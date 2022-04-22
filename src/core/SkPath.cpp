/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPath.h"
#include <cmath>
#include <utility>
#include "include/core/SkMath.h"
#include "include/core/SkRRect.h"
#include "include/private/SkPathRef.h"
#include "include/private/SkTo.h"
#include "src/core/SkCubicClipper.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkPathMakers.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkTLazy.h"
#include "src/pathops/SkPathOpsPoint.h"
#include "src/gpu/geometry/GrAATriangulator.h"

namespace pk {
static float poly_eval(float A, float B, float C, float t) {
  return (A * t + B) * t + C;
}

static float poly_eval(float A, float B, float C, float D, float t) {
  return ((A * t + B) * t + C) * t + D;
}

////////////////////////////////////////////////////////////////////////////

/**
 *  Path.bounds is defined to be the bounds of all the control points.
 *  If we called bounds.join(r) we would skip r if r was empty, which breaks
 *  our promise. Hence we have a custom joiner that doesn't look at emptiness
 */
static void joinNoEmptyChecks(SkRect* dst, const SkRect& src) {
  dst->fLeft = std::min(dst->fLeft, src.fLeft);
  dst->fTop = std::min(dst->fTop, src.fTop);
  dst->fRight = std::max(dst->fRight, src.fRight);
  dst->fBottom = std::max(dst->fBottom, src.fBottom);
}

static bool is_degenerate(const SkPath& path) {
  return (path.countVerbs() - SkPathPriv::LeadingMoveToCount(path)) == 0;
}

class SkAutoDisableDirectionCheck {
 public:
  SkAutoDisableDirectionCheck(SkPath* path) : fPath(path) {
    fSaved = static_cast<SkPathFirstDirection>(fPath->getFirstDirection());
  }

  ~SkAutoDisableDirectionCheck() {
    fPath->setFirstDirection(fSaved);
  }

 private:
  SkPath* fPath;
  SkPathFirstDirection fSaved;
};

/*  This class's constructor/destructor bracket a path editing operation. It is
    used when we know the bounds of the amount we are going to add to the path
    (usually a new contour, but not required).

    It captures some state about the path up front (i.e. if it already has a
    cached bounds), and then if it can, it updates the cache bounds explicitly,
    avoiding the need to revisit all of the points in getBounds().

    It also notes if the path was originally degenerate, and if so, sets
    isConvex to true. Thus it can only be used if the contour being added is
    convex.
 */
class SkAutoPathBoundsUpdate {
 public:
  SkAutoPathBoundsUpdate(SkPath* path, const SkRect& r) : fPath(path), fRect(r) {
    // Cannot use fRect for our bounds unless we know it is sorted
    fRect.sort();
    // Mark the path's bounds as dirty if (1) they are, or (2) the path
    // is non-finite, and therefore its bounds are not meaningful
    fHasValidBounds = path->hasComputedBounds() && path->isFinite();
    fEmpty = path->isEmpty();
    if (fHasValidBounds && !fEmpty) {
      joinNoEmptyChecks(&fRect, fPath->getBounds());
    }
    fDegenerate = is_degenerate(*path);
  }

  ~SkAutoPathBoundsUpdate() {
    fPath->setConvexity(fDegenerate ? SkPathConvexity::kConvex : SkPathConvexity::kUnknown);
    if ((fEmpty || fHasValidBounds) && fRect.isFinite()) {
      fPath->setBounds(fRect);
    }
  }

 private:
  SkPath* fPath;
  SkRect fRect;
  bool fHasValidBounds;
  bool fDegenerate;
  bool fEmpty;
};

////////////////////////////////////////////////////////////////////////////

/*
    Stores the verbs and points as they are given to us, with exceptions:
    - we only record "Close" if it was immediately preceeded by Move | Line | Quad | Cubic
    - we insert a Move(0,0) if Line | Quad | Cubic is our first command

    The iterator does more cleanup, especially if forceClose == true
    1. If we encounter degenerate segments, remove them
    2. if we encounter Close, return a cons'd up Line() first (if the curr-pt != start-pt)
    3. if we encounter Move without a preceeding Close, and forceClose is true, goto #2
    4. if we encounter Line | Quad | Cubic after Close, cons up a Move
*/

////////////////////////////////////////////////////////////////////////////

// flag to require a moveTo if we begin with something else, like lineTo etc.
// This will also be the value of lastMoveToIndex for a single contour
// ending with close, so countVerbs needs to be checked against 0.
#define INITIAL_LASTMOVETOINDEX_VALUE ~0

SkPath::SkPath() : fPathRef(SkPathRef::CreateEmpty()) {
  this->resetFields();
}

SkPath::SkPath(sk_sp<SkPathRef> pr, SkPathFillType ft, SkPathConvexity ct,
               SkPathFirstDirection firstDirection)
    : fPathRef(std::move(pr)),
      fLastMoveToIndex(INITIAL_LASTMOVETOINDEX_VALUE),
      fConvexity((uint8_t)ct),
      fFirstDirection((uint8_t)firstDirection),
      fFillType((unsigned)ft) {
}

void SkPath::resetFields() {
  // fPathRef is assumed to have been emptied by the caller.
  fLastMoveToIndex = INITIAL_LASTMOVETOINDEX_VALUE;
  fFillType = SkToU8(SkPathFillType::kWinding);
  this->setConvexity(SkPathConvexity::kUnknown);
  this->setFirstDirection(SkPathFirstDirection::kUnknown);

  // We don't touch Android's fSourcePath.  It's used to track texture garbage collection, so we
  // don't want to muck with it if it's been set to something non-nullptr.
}

SkPath::SkPath(const SkPath& that) : fPathRef(SkRef(that.fPathRef.get())) {
  this->copyFields(that);
}

SkPath& SkPath::operator=(const SkPath& that) {
  if (this != &that) {
    fPathRef.reset(SkRef(that.fPathRef.get()));
    this->copyFields(that);
  }
  return *this;
}

void SkPath::copyFields(const SkPath& that) {
  // fPathRef is assumed to have been set by the caller.
  fLastMoveToIndex = that.fLastMoveToIndex;
  fFillType = that.fFillType;

  // Non-atomic assignment of atomic values.
  this->setConvexity(that.getConvexityOrUnknown());
  this->setFirstDirection(that.getFirstDirection());
}

bool operator==(const SkPath& a, const SkPath& b) {
  // note: don't need to look at isConvex or bounds, since just comparing the
  // raw data is sufficient.
  return &a == &b || (a.fFillType == b.fFillType && *a.fPathRef == *b.fPathRef);
}

void SkPath::swap(SkPath& that) {
  if (this != &that) {
    fPathRef.swap(that.fPathRef);
    std::swap(fLastMoveToIndex, that.fLastMoveToIndex);

    const auto ft = fFillType;
    fFillType = that.fFillType;
    that.fFillType = ft;

    // Non-atomic swaps of atomic values.
    SkPathConvexity c = this->getConvexityOrUnknown();
    this->setConvexity(that.getConvexityOrUnknown());
    that.setConvexity(c);

    SkPathFirstDirection fd = this->getFirstDirection();
    this->setFirstDirection(that.getFirstDirection());
    that.setFirstDirection(fd);
  }
}

bool SkPath::isInterpolatable(const SkPath& compare) const {
  // need the same structure (verbs, conicweights) and same point-count
  return fPathRef->fPoints.count() == compare.fPathRef->fPoints.count() &&
         fPathRef->fVerbs == compare.fPathRef->fVerbs &&
         fPathRef->fConicWeights == compare.fPathRef->fConicWeights;
}

bool SkPath::interpolate(const SkPath& ending, SkScalar weight, SkPath* out) const {
  int pointCount = fPathRef->countPoints();
  if (pointCount != ending.fPathRef->countPoints()) {
    return false;
  }
  if (!pointCount) {
    return true;
  }
  out->reset();
  out->addPath(*this);
  fPathRef->interpolate(*ending.fPathRef, weight, out->fPathRef.get());
  return true;
}

static inline bool check_edge_against_rect(const SkPoint& p0, const SkPoint& p1, const SkRect& rect,
                                           SkPathFirstDirection dir) {
  const SkPoint* edgeBegin;
  SkVector v;
  if (SkPathFirstDirection::kCW == dir) {
    v = p1 - p0;
    edgeBegin = &p0;
  } else {
    v = p0 - p1;
    edgeBegin = &p1;
  }
  if (v.fX || v.fY) {
    // check the cross product of v with the vec from edgeBegin to each rect corner
    SkScalar yL = v.fY * (rect.fLeft - edgeBegin->fX);
    SkScalar xT = v.fX * (rect.fTop - edgeBegin->fY);
    SkScalar yR = v.fY * (rect.fRight - edgeBegin->fX);
    SkScalar xB = v.fX * (rect.fBottom - edgeBegin->fY);
    if ((xT < yL) || (xT < yR) || (xB < yL) || (xB < yR)) {
      return false;
    }
  }
  return true;
}

bool SkPath::conservativelyContainsRect(const SkRect& rect) const {
  // This only handles non-degenerate convex paths currently.
  if (!this->isConvex()) {
    return false;
  }

  SkPathFirstDirection direction = SkPathPriv::ComputeFirstDirection(*this);
  if (direction == SkPathFirstDirection::kUnknown) {
    return false;
  }

  SkPoint firstPt;
  SkPoint prevPt;
  int segmentCount = 0;

  for (auto iter: SkPathPriv::Iterate(*this)) {
    auto verb = std::get<0>(iter);
    auto pts = std::get<1>(iter);
    auto weight = std::get<2>(iter);
    if (verb == SkPathVerb::kClose || (segmentCount > 0 && verb == SkPathVerb::kMove)) {
      segmentCount++;
      break;
    } else if (verb == SkPathVerb::kMove) {
      firstPt = prevPt = pts[0];
    } else {
      int pointCount = SkPathPriv::PtsInVerb((unsigned)verb);

      if (!SkPathPriv::AllPointsEq(pts, pointCount + 1)) {
        int nextPt = pointCount;
        segmentCount++;

        if (SkPathVerb::kConic == verb) {
          SkConic orig;
          orig.set(pts, *weight);
          SkPoint quadPts[5];
          int count = orig.chopIntoQuadsPOW2(quadPts, 1);
          PkASSERT_RELEASE(2 == count);

          if (!check_edge_against_rect(quadPts[0], quadPts[2], rect, direction)) {
            return false;
          }
          if (!check_edge_against_rect(quadPts[2], quadPts[4], rect, direction)) {
            return false;
          }
        } else {
          if (!check_edge_against_rect(prevPt, pts[nextPt], rect, direction)) {
            return false;
          }
        }
        prevPt = pts[nextPt];
      }
    }
  }

  if (segmentCount) {
    return check_edge_against_rect(prevPt, firstPt, rect, direction);
  }
  return false;
}

SkPath& SkPath::reset() {
  fPathRef.reset(SkPathRef::CreateEmpty());
  this->resetFields();
  return *this;
}

SkPath& SkPath::rewind() {
  SkPathRef::Rewind(&fPathRef);
  this->resetFields();
  return *this;
}

bool SkPath::isLastContourClosed() const {
  int verbCount = fPathRef->countVerbs();
  if (0 == verbCount) {
    return false;
  }
  return kClose_Verb == fPathRef->atVerb(verbCount - 1);
}

bool SkPath::isLine(SkPoint line[2]) const {
  int verbCount = fPathRef->countVerbs();

  if (2 == verbCount) {
    if (kLine_Verb == fPathRef->atVerb(1)) {
      if (line) {
        const SkPoint* pts = fPathRef->points();
        line[0] = pts[0];
        line[1] = pts[1];
      }
      return true;
    }
  }
  return false;
}

/*
 Determines if path is a rect by keeping track of changes in direction
 and looking for a loop either clockwise or counterclockwise.

 The direction is computed such that:
  0: vertical up
  1: horizontal left
  2: vertical down
  3: horizontal right

A rectangle cycles up/right/down/left or up/left/down/right.

The test fails if:
  The path is closed, and followed by a line.
  A second move creates a new endpoint.
  A diagonal line is parsed.
  There's more than four changes of direction.
  There's a discontinuity on the line (e.g., a move in the middle)
  The line reverses direction.
  The path contains a quadratic or cubic.
  The path contains fewer than four points.
 *The rectangle doesn't complete a cycle.
 *The final point isn't equal to the first point.

  *These last two conditions we relax if we have a 3-edge path that would
   form a rectangle if it were closed (as we do when we fill a path)

It's OK if the path has:
  Several colinear line segments composing a rectangle side.
  Single points on the rectangle side.

The direction takes advantage of the corners found since opposite sides
must travel in opposite directions.

FIXME: Allow colinear quads and cubics to be treated like lines.
FIXME: If the API passes fill-only, return true if the filled stroke
       is a rectangle, though the caller failed to close the path.

 directions values:
    0x1 is set if the segment is horizontal
    0x2 is set if the segment is moving to the right or down
 thus:
    two directions are opposites iff (dirA ^ dirB) == 0x2
    two directions are perpendicular iff (dirA ^ dirB) == 0x1

 */
static int rect_make_dir(SkScalar dx, SkScalar dy) {
  return ((0 != dx) << 0) | ((dx > 0 || dy > 0) << 1);
}

bool SkPath::isRect(SkRect* rect, bool* isClosed, SkPathDirection* direction) const {
  int currVerb = 0;
  const SkPoint* pts = fPathRef->points();
  return SkPathPriv::IsRectContour(*this, false, &currVerb, &pts, isClosed, direction, rect);
}

bool SkPath::isOval(SkRect* bounds) const {
  return SkPathPriv::IsOval(*this, bounds, nullptr, nullptr);
}

bool SkPath::isRRect(SkRRect* rrect) const {
  return SkPathPriv::IsRRect(*this, rrect, nullptr, nullptr);
}

int SkPath::countPoints() const {
  return fPathRef->countPoints();
}

int SkPath::getPoints(SkPoint dst[], int max) const {
  int count = std::min(max, fPathRef->countPoints());
  sk_careful_memcpy(dst, fPathRef->points(), count * sizeof(SkPoint));
  return fPathRef->countPoints();
}

SkPoint SkPath::getPoint(int index) const {
  if ((unsigned)index < (unsigned)fPathRef->countPoints()) {
    return fPathRef->atPoint(index);
  }
  return SkPoint::Make(0, 0);
}

int SkPath::countVerbs() const {
  return fPathRef->countVerbs();
}

int SkPath::getVerbs(uint8_t dst[], int max) const {
  int count = std::min(max, fPathRef->countVerbs());
  if (count) {
    memcpy(dst, fPathRef->verbsBegin(), count);
  }
  return fPathRef->countVerbs();
}

bool SkPath::getLastPt(SkPoint* lastPt) const {
  int count = fPathRef->countPoints();
  if (count > 0) {
    if (lastPt) {
      *lastPt = fPathRef->atPoint(count - 1);
    }
    return true;
  }
  if (lastPt) {
    lastPt->set(0, 0);
  }
  return false;
}

void SkPath::setPt(int index, SkScalar x, SkScalar y) {
  int count = fPathRef->countPoints();
  if (count <= index) {
    return;
  } else {
    SkPathRef::Editor ed(&fPathRef);
    ed.atPoint(index)->set(x, y);
  }
}

void SkPath::setLastPt(SkScalar x, SkScalar y) {
  int count = fPathRef->countPoints();
  if (count == 0) {
    this->moveTo(x, y);
  } else {
    SkPathRef::Editor ed(&fPathRef);
    ed.atPoint(count - 1)->set(x, y);
  }
}

// This is the public-facing non-const setConvexity().
void SkPath::setConvexity(SkPathConvexity c) {
  fConvexity.store((uint8_t)c, std::memory_order_relaxed);
}

// Const hooks for working with fConvexity and fFirstDirection from const methods.
void SkPath::setConvexity(SkPathConvexity c) const {
  fConvexity.store((uint8_t)c, std::memory_order_relaxed);
}

void SkPath::setFirstDirection(SkPathFirstDirection d) const {
  fFirstDirection.store((uint8_t)d, std::memory_order_relaxed);
}

SkPathFirstDirection SkPath::getFirstDirection() const {
  return (SkPathFirstDirection)fFirstDirection.load(std::memory_order_relaxed);
}

SkPathConvexity SkPath::getConvexity() const {
  SkPathConvexity convexity = this->getConvexityOrUnknown();
  if (convexity == SkPathConvexity::kUnknown) {
    convexity = this->computeConvexity();
  }
  return convexity;
}

//////////////////////////////////////////////////////////////////////////////
//  Construction methods

SkPath& SkPath::dirtyAfterEdit() {
  this->setConvexity(SkPathConvexity::kUnknown);
  this->setFirstDirection(SkPathFirstDirection::kUnknown);
  return *this;
}

void SkPath::incReserve(int inc) {
  if (inc > 0) {
    SkPathRef::Editor(&fPathRef, inc, inc);
  }
}

SkPath& SkPath::moveTo(SkScalar x, SkScalar y) {
  SkPathRef::Editor ed(&fPathRef);

  // remember our index
  fLastMoveToIndex = fPathRef->countPoints();

  ed.growForVerb(kMove_Verb)->set(x, y);

  return this->dirtyAfterEdit();
}

void SkPath::injectMoveToIfNeeded() {
  if (fLastMoveToIndex < 0) {
    SkScalar x, y;
    if (fPathRef->countVerbs() == 0) {
      x = y = 0;
    } else {
      const SkPoint& pt = fPathRef->atPoint(~fLastMoveToIndex);
      x = pt.fX;
      y = pt.fY;
    }
    this->moveTo(x, y);
  }
}

SkPath& SkPath::lineTo(SkScalar x, SkScalar y) {
  this->injectMoveToIfNeeded();

  SkPathRef::Editor ed(&fPathRef);
  ed.growForVerb(kLine_Verb)->set(x, y);

  return this->dirtyAfterEdit();
}

SkPath& SkPath::quadTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2) {
  this->injectMoveToIfNeeded();

  SkPathRef::Editor ed(&fPathRef);
  SkPoint* pts = ed.growForVerb(kQuad_Verb);
  pts[0].set(x1, y1);
  pts[1].set(x2, y2);

  return this->dirtyAfterEdit();
}

SkPath& SkPath::conicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2, SkScalar w) {
  // check for <= 0 or NaN with this test
  if (!(w > 0)) {
    this->lineTo(x2, y2);
  } else if (!SkScalarIsFinite(w)) {
    this->lineTo(x1, y1);
    this->lineTo(x2, y2);
  } else if (PK_Scalar1 == w) {
    this->quadTo(x1, y1, x2, y2);
  } else {
    this->injectMoveToIfNeeded();

    SkPathRef::Editor ed(&fPathRef);
    SkPoint* pts = ed.growForVerb(kConic_Verb, w);
    pts[0].set(x1, y1);
    pts[1].set(x2, y2);

    (void)this->dirtyAfterEdit();
  }
  return *this;
}

SkPath& SkPath::cubicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2, SkScalar x3,
                        SkScalar y3) {
  this->injectMoveToIfNeeded();

  SkPathRef::Editor ed(&fPathRef);
  SkPoint* pts = ed.growForVerb(kCubic_Verb);
  pts[0].set(x1, y1);
  pts[1].set(x2, y2);
  pts[2].set(x3, y3);

  return this->dirtyAfterEdit();
}

SkPath& SkPath::close() {
  int count = fPathRef->countVerbs();
  if (count > 0) {
    switch (fPathRef->atVerb(count - 1)) {
      case kLine_Verb:
      case kQuad_Verb:
      case kConic_Verb:
      case kCubic_Verb:
      case kMove_Verb: {
        SkPathRef::Editor ed(&fPathRef);
        ed.growForVerb(kClose_Verb);
        break;
      }
      case kClose_Verb:
        // don't add a close if it's the first verb or a repeat
        break;
      default:
        PkDEBUGFAIL("unexpected verb");
        break;
    }
  }

  // signal that we need a moveTo to follow us (unless we're done)
#if 0
  if (fLastMoveToIndex >= 0) {
      fLastMoveToIndex = ~fLastMoveToIndex;
  }
#else
  fLastMoveToIndex ^= ~fLastMoveToIndex >> (8 * sizeof(fLastMoveToIndex) - 1);
#endif
  return *this;
}

///////////////////////////////////////////////////////////////////////////////

SkPath& SkPath::addRect(const SkRect& rect, SkPathDirection dir, unsigned startIndex) {
  this->setFirstDirection(this->hasOnlyMoveTos() ? (SkPathFirstDirection)dir
                                                 : SkPathFirstDirection::kUnknown);
  SkAutoDisableDirectionCheck addc(this);
  SkAutoPathBoundsUpdate apbu(this, rect);

  const int kVerbs = 5;  // moveTo + 3x lineTo + close
  this->incReserve(kVerbs);

  SkPath_RectPointIterator iter(rect, dir, startIndex);

  this->moveTo(iter.current());
  this->lineTo(iter.next());
  this->lineTo(iter.next());
  this->lineTo(iter.next());
  this->close();
  return *this;
}

SkPath& SkPath::addPoly(const SkPoint pts[], int count, bool close) {
  if (count <= 0) {
    return *this;
  }
  fLastMoveToIndex = fPathRef->countPoints();

  // +close makes room for the extra kClose_Verb
  SkPathRef::Editor ed(&fPathRef, count + close, count);

  ed.growForVerb(kMove_Verb)->set(pts[0].fX, pts[0].fY);
  if (count > 1) {
    SkPoint* p = ed.growForRepeatedVerb(kLine_Verb, count - 1);
    memcpy(p, &pts[1], (count - 1) * sizeof(SkPoint));
  }

  if (close) {
    ed.growForVerb(kClose_Verb);
    fLastMoveToIndex ^= ~fLastMoveToIndex >> (8 * sizeof(fLastMoveToIndex) - 1);
  }

  (void)this->dirtyAfterEdit();
  return *this;
}

SkPath& SkPath::addRRect(const SkRRect& rrect, SkPathDirection dir) {
  // legacy start indices: 6 (CW) and 7(CCW)
  return this->addRRect(rrect, dir, dir == SkPathDirection::kCW ? 6 : 7);
}

SkPath& SkPath::addRRect(const SkRRect& rrect, SkPathDirection dir, unsigned startIndex) {
  bool isRRect = hasOnlyMoveTos();
  const SkRect& bounds = rrect.getBounds();

  if (rrect.isRect() || rrect.isEmpty()) {
    // degenerate(rect) => radii points are collapsing
    this->addRect(bounds, dir, (startIndex + 1) / 2);
  } else if (rrect.isOval()) {
    // degenerate(oval) => line points are collapsing
    this->addOval(bounds, dir, startIndex / 2);
  } else {
    this->setFirstDirection(this->hasOnlyMoveTos() ? (SkPathFirstDirection)dir
                                                   : SkPathFirstDirection::kUnknown);

    SkAutoPathBoundsUpdate apbu(this, bounds);
    SkAutoDisableDirectionCheck addc(this);

    // we start with a conic on odd indices when moving CW vs. even indices when moving CCW
    const bool startsWithConic = ((startIndex & 1) == (dir == SkPathDirection::kCW));
    const SkScalar weight = PK_ScalarRoot2Over2;

    const int kVerbs = startsWithConic ? 9    // moveTo + 4x conicTo + 3x lineTo + close
                                       : 10;  // moveTo + 4x lineTo + 4x conicTo + close
    this->incReserve(kVerbs);

    SkPath_RRectPointIterator rrectIter(rrect, dir, startIndex);
    // Corner iterator indices follow the collapsed radii model,
    // adjusted such that the start pt is "behind" the radii start pt.
    const unsigned rectStartIndex = startIndex / 2 + (dir == SkPathDirection::kCW ? 0 : 1);
    SkPath_RectPointIterator rectIter(bounds, dir, rectStartIndex);

    this->moveTo(rrectIter.current());
    if (startsWithConic) {
      for (unsigned i = 0; i < 3; ++i) {
        this->conicTo(rectIter.next(), rrectIter.next(), weight);
        this->lineTo(rrectIter.next());
      }
      this->conicTo(rectIter.next(), rrectIter.next(), weight);
      // final lineTo handled by close().
    } else {
      for (unsigned i = 0; i < 4; ++i) {
        this->lineTo(rrectIter.next());
        this->conicTo(rectIter.next(), rrectIter.next(), weight);
      }
    }
    this->close();
    SkPathRef::Editor ed(&fPathRef);
    ed.setIsRRect(isRRect, dir == SkPathDirection::kCCW, startIndex % 8);
  }
  return *this;
}

bool SkPath::hasOnlyMoveTos() const {
  int count = fPathRef->countVerbs();
  const uint8_t* verbs = fPathRef->verbsBegin();
  for (int i = 0; i < count; ++i) {
    if (*verbs == kLine_Verb || *verbs == kQuad_Verb || *verbs == kConic_Verb ||
        *verbs == kCubic_Verb) {
      return false;
    }
    ++verbs;
  }
  return true;
}

bool SkPath::isZeroLengthSincePoint(int startPtIndex) const {
  int count = fPathRef->countPoints() - startPtIndex;
  if (count < 2) {
    return true;
  }
  const SkPoint* pts = fPathRef->points() + startPtIndex;
  const SkPoint& first = *pts;
  for (int index = 1; index < count; ++index) {
    if (first != pts[index]) {
      return false;
    }
  }
  return true;
}

SkPath& SkPath::addRoundRect(const SkRect& rect, SkScalar rx, SkScalar ry, SkPathDirection dir) {
  if (rx < 0 || ry < 0) {
    return *this;
  }

  SkRRect rrect;
  rrect.setRectXY(rect, rx, ry);
  return this->addRRect(rrect, dir);
}

SkPath& SkPath::addOval(const SkRect& oval, SkPathDirection dir) {
  // legacy start index: 1
  return this->addOval(oval, dir, 1);
}

SkPath& SkPath::addOval(const SkRect& oval, SkPathDirection dir, unsigned startPointIndex) {
  /* If addOval() is called after previous moveTo(),
     this path is still marked as an oval. This is used to
     fit into WebKit's calling sequences.
     We can't simply check isEmpty() in this case, as additional
     moveTo() would mark the path non empty.
   */
  bool isOval = hasOnlyMoveTos();
  if (isOval) {
    this->setFirstDirection((SkPathFirstDirection)dir);
  } else {
    this->setFirstDirection(SkPathFirstDirection::kUnknown);
  }

  SkAutoDisableDirectionCheck addc(this);
  SkAutoPathBoundsUpdate apbu(this, oval);

  const int kVerbs = 6;  // moveTo + 4x conicTo + close
  this->incReserve(kVerbs);

  SkPath_OvalPointIterator ovalIter(oval, dir, startPointIndex);
  // The corner iterator pts are tracking "behind" the oval/radii pts.
  SkPath_RectPointIterator rectIter(oval, dir,
                                    startPointIndex + (dir == SkPathDirection::kCW ? 0 : 1));
  const SkScalar weight = PK_ScalarRoot2Over2;

  this->moveTo(ovalIter.current());
  for (unsigned i = 0; i < 4; ++i) {
    this->conicTo(rectIter.next(), ovalIter.next(), weight);
  }
  this->close();
  SkPathRef::Editor ed(&fPathRef);
  ed.setIsOval(isOval, SkPathDirection::kCCW == dir, startPointIndex % 4);
  return *this;
}

SkPath& SkPath::addCircle(SkScalar x, SkScalar y, SkScalar r, SkPathDirection dir) {
  if (r > 0) {
    this->addOval(SkRect::MakeLTRB(x - r, y - r, x + r, y + r), dir);
  }
  return *this;
}

SkPath& SkPath::addPath(const SkPath& path, SkScalar dx, SkScalar dy, AddPathMode mode) {
  SkMatrix matrix;

  matrix.setTranslate(dx, dy);
  return this->addPath(path, matrix, mode);
}

SkPath& SkPath::addPath(const SkPath& srcPath, const SkMatrix& matrix, AddPathMode mode) {
  if (srcPath.isEmpty()) {
    return *this;
  }

  // Detect if we're trying to add ourself
  const SkPath* src = &srcPath;
  SkTLazy<SkPath> tmp;
  if (this == src) {
    src = tmp.set(srcPath);
  }

  if (kAppend_AddPathMode == mode && !matrix.hasPerspective()) {
    fLastMoveToIndex = this->countPoints() + src->fLastMoveToIndex;

    SkPathRef::Editor ed(&fPathRef);
    auto result = ed.growForVerbsInPath(*src->fPathRef);
    auto newPts = std::get<0>(result);
    auto newWeights = std::get<1>(result);
    matrix.mapPoints(newPts, src->fPathRef->points(), src->countPoints());
    if (int numWeights = src->fPathRef->countWeights()) {
      memcpy(newWeights, src->fPathRef->conicWeights(), numWeights * sizeof(newWeights[0]));
    }
    // fiddle with fLastMoveToIndex, as we do in SkPath::close()
    if ((SkPathVerb)fPathRef->verbsEnd()[-1] == SkPathVerb::kClose) {
      fLastMoveToIndex ^= ~fLastMoveToIndex >> (8 * sizeof(fLastMoveToIndex) - 1);
    }
    return this->dirtyAfterEdit();
  }

  SkMatrixPriv::MapPtsProc mapPtsProc = SkMatrixPriv::GetMapPtsProc(matrix);
  bool firstVerb = true;
  for (auto iter: SkPathPriv::Iterate(*src)) {
    auto verb = std::get<0>(iter);
    auto pts = std::get<1>(iter);
    auto w = std::get<2>(iter);
    switch (verb) {
      SkPoint mappedPts[3];
      case SkPathVerb::kMove:
        mapPtsProc(matrix, mappedPts, &pts[0], 1);
        if (firstVerb && mode == kExtend_AddPathMode && !isEmpty()) {
          injectMoveToIfNeeded();  // In case last contour is closed
          SkPoint lastPt;
          // don't add lineTo if it is degenerate
          if (fLastMoveToIndex < 0 || !this->getLastPt(&lastPt) || lastPt != mappedPts[0]) {
            this->lineTo(mappedPts[0]);
          }
        } else {
          this->moveTo(mappedPts[0]);
        }
        break;
      case SkPathVerb::kLine:
        mapPtsProc(matrix, mappedPts, &pts[1], 1);
        this->lineTo(mappedPts[0]);
        break;
      case SkPathVerb::kQuad:
        mapPtsProc(matrix, mappedPts, &pts[1], 2);
        this->quadTo(mappedPts[0], mappedPts[1]);
        break;
      case SkPathVerb::kConic:
        mapPtsProc(matrix, mappedPts, &pts[1], 2);
        this->conicTo(mappedPts[0], mappedPts[1], *w);
        break;
      case SkPathVerb::kCubic:
        mapPtsProc(matrix, mappedPts, &pts[1], 3);
        this->cubicTo(mappedPts[0], mappedPts[1], mappedPts[2]);
        break;
      case SkPathVerb::kClose:
        this->close();
        break;
    }
    firstVerb = false;
  }
  return *this;
}

///////////////////////////////////////////////////////////////////////////////

// ignore the last point of the 1st contour
SkPath& SkPath::reversePathTo(const SkPath& path) {
  if (path.fPathRef->fVerbs.count() == 0) {
    return *this;
  }

  const uint8_t* verbs = path.fPathRef->verbsEnd();
  const uint8_t* verbsBegin = path.fPathRef->verbsBegin();
  const SkPoint* pts = path.fPathRef->pointsEnd() - 1;
  const SkScalar* conicWeights = path.fPathRef->conicWeightsEnd();

  while (verbs > verbsBegin) {
    uint8_t v = *--verbs;
    pts -= SkPathPriv::PtsInVerb(v);
    switch (v) {
      case kMove_Verb:
        // if the path has multiple contours, stop after reversing the last
        return *this;
      case kLine_Verb:
        this->lineTo(pts[0]);
        break;
      case kQuad_Verb:
        this->quadTo(pts[1], pts[0]);
        break;
      case kConic_Verb:
        this->conicTo(pts[1], pts[0], *--conicWeights);
        break;
      case kCubic_Verb:
        this->cubicTo(pts[2], pts[1], pts[0]);
        break;
      case kClose_Verb:
        break;
      default:
        PkDEBUGFAIL("bad verb");
        break;
    }
  }
  return *this;
}

SkPath& SkPath::reverseAddPath(const SkPath& srcPath) {
  // Detect if we're trying to add ourself
  const SkPath* src = &srcPath;
  SkTLazy<SkPath> tmp;
  if (this == src) {
    src = tmp.set(srcPath);
  }

  const uint8_t* verbsBegin = src->fPathRef->verbsBegin();
  const uint8_t* verbs = src->fPathRef->verbsEnd();
  const SkPoint* pts = src->fPathRef->pointsEnd();
  const SkScalar* conicWeights = src->fPathRef->conicWeightsEnd();

  bool needMove = true;
  bool needClose = false;
  while (verbs > verbsBegin) {
    uint8_t v = *--verbs;
    int n = SkPathPriv::PtsInVerb(v);

    if (needMove) {
      --pts;
      this->moveTo(pts->fX, pts->fY);
      needMove = false;
    }
    pts -= n;
    switch (v) {
      case kMove_Verb:
        if (needClose) {
          this->close();
          needClose = false;
        }
        needMove = true;
        pts += 1;  // so we see the point in "if (needMove)" above
        break;
      case kLine_Verb:
        this->lineTo(pts[0]);
        break;
      case kQuad_Verb:
        this->quadTo(pts[1], pts[0]);
        break;
      case kConic_Verb:
        this->conicTo(pts[1], pts[0], *--conicWeights);
        break;
      case kCubic_Verb:
        this->cubicTo(pts[2], pts[1], pts[0]);
        break;
      case kClose_Verb:
        needClose = true;
        break;
      default:
        PkDEBUGFAIL("unexpected verb");
    }
  }
  return *this;
}

static void subdivide_cubic_to(SkPath* path, const SkPoint pts[4], int level = 2) {
  if (--level >= 0) {
    SkPoint tmp[7];

    SkChopCubicAtHalf(pts, tmp);
    subdivide_cubic_to(path, &tmp[0], level);
    subdivide_cubic_to(path, &tmp[3], level);
  } else {
    path->cubicTo(pts[1], pts[2], pts[3]);
  }
}

void SkPath::transform(const SkMatrix& matrix, SkPath* dst) const {
  if (matrix.isIdentity()) {
    if (dst != nullptr && dst != this) {
      *dst = *this;
    }
    return;
  }

  if (dst == nullptr) {
    dst = (SkPath*)this;
  }
  if (matrix.hasPerspective()) {
    SkPath tmp;
    tmp.fFillType = fFillType;

    SkPath clipped;
    const SkPath* src = this;
    SkPath::Iter iter(*src, false);
    SkPoint pts[4];
    SkPath::Verb verb;

    while ((verb = iter.next(pts)) != kDone_Verb) {
      switch (verb) {
        case kMove_Verb:
          tmp.moveTo(pts[0]);
          break;
        case kLine_Verb:
          tmp.lineTo(pts[1]);
          break;
        case kQuad_Verb:
          // promote the quad to a conic
          tmp.conicTo(pts[1], pts[2], SkConic::TransformW(pts, PK_Scalar1, matrix));
          break;
        case kConic_Verb:
          tmp.conicTo(pts[1], pts[2], SkConic::TransformW(pts, iter.conicWeight(), matrix));
          break;
        case kCubic_Verb:
          subdivide_cubic_to(&tmp, pts);
          break;
        case kClose_Verb:
          tmp.close();
          break;
        default:
          PkDEBUGFAIL("unknown verb");
          break;
      }
    }

    dst->swap(tmp);
    SkPathRef::Editor ed(&dst->fPathRef);
    matrix.mapPoints(ed.writablePoints(), ed.pathRef()->countPoints());
    dst->setFirstDirection(SkPathFirstDirection::kUnknown);
  } else {
    SkPathConvexity convexity = this->getConvexityOrUnknown();

    SkPathRef::CreateTransformedCopy(&dst->fPathRef, *fPathRef, matrix);

    if (this != dst) {
      dst->fLastMoveToIndex = fLastMoveToIndex;
      dst->fFillType = fFillType;
    }

    // Due to finite/fragile float numerics, we can't assume that a convex path remains
    // convex after a transformation, so mark it as unknown here.
    // However, some transformations are thought to be safe:
    //    axis-aligned values under scale/translate.
    //
    if (convexity == SkPathConvexity::kConvex &&
        (!matrix.isScaleTranslate() || !SkPathPriv::IsAxisAligned(*this))) {
      // Not safe to still assume we're convex...
      convexity = SkPathConvexity::kUnknown;
    }
    dst->setConvexity(convexity);

    if (this->getFirstDirection() == SkPathFirstDirection::kUnknown) {
      dst->setFirstDirection(SkPathFirstDirection::kUnknown);
    } else {
      SkScalar det2x2 = matrix.get(SkMatrix::kMScaleX) * matrix.get(SkMatrix::kMScaleY) -
                        matrix.get(SkMatrix::kMSkewX) * matrix.get(SkMatrix::kMSkewY);
      if (det2x2 < 0) {
        dst->setFirstDirection(
            SkPathPriv::OppositeFirstDirection((SkPathFirstDirection)this->getFirstDirection()));
      } else if (det2x2 > 0) {
        dst->setFirstDirection(this->getFirstDirection());
      } else {
        dst->setFirstDirection(SkPathFirstDirection::kUnknown);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

SkPath::Iter::Iter() {
  // need to init enough to make next() harmlessly return kDone_Verb
  fVerbs = nullptr;
  fVerbStop = nullptr;
  fNeedClose = false;
}

SkPath::Iter::Iter(const SkPath& path, bool forceClose) {
  this->setPath(path, forceClose);
}

void SkPath::Iter::setPath(const SkPath& path, bool forceClose) {
  fPts = path.fPathRef->points();
  fVerbs = path.fPathRef->verbsBegin();
  fVerbStop = path.fPathRef->verbsEnd();
  fConicWeights = path.fPathRef->conicWeights();
  if (fConicWeights) {
    fConicWeights -= 1;  // begin one behind
  }
  fLastPt.fX = fLastPt.fY = 0;
  fMoveTo.fX = fMoveTo.fY = 0;
  fForceClose = SkToU8(forceClose);
  fNeedClose = false;
}

bool SkPath::Iter::isClosedContour() const {
  if (fVerbs == nullptr || fVerbs == fVerbStop) {
    return false;
  }
  if (fForceClose) {
    return true;
  }

  const uint8_t* verbs = fVerbs;
  const uint8_t* stop = fVerbStop;

  if (kMove_Verb == *verbs) {
    verbs += 1;  // skip the initial moveto
  }

  while (verbs < stop) {
    // verbs points one beyond the current verb, decrement first.
    unsigned v = *verbs++;
    if (kMove_Verb == v) {
      break;
    }
    if (kClose_Verb == v) {
      return true;
    }
  }
  return false;
}

SkPath::Verb SkPath::Iter::autoClose(SkPoint pts[2]) {
  if (fLastPt != fMoveTo) {
    // A special case: if both points are NaN, SkPoint::operation== returns
    // false, but the iterator expects that they are treated as the same.
    // (consider SkPoint is a 2-dimension float point).
    if (SkScalarIsNaN(fLastPt.fX) || SkScalarIsNaN(fLastPt.fY) || SkScalarIsNaN(fMoveTo.fX) ||
        SkScalarIsNaN(fMoveTo.fY)) {
      return kClose_Verb;
    }

    pts[0] = fLastPt;
    pts[1] = fMoveTo;
    fLastPt = fMoveTo;
    fCloseLine = true;
    return kLine_Verb;
  } else {
    pts[0] = fMoveTo;
    return kClose_Verb;
  }
}

SkPath::Verb SkPath::Iter::next(SkPoint ptsParam[4]) {
  if (fVerbs == fVerbStop) {
    // Close the curve if requested and if there is some curve to close
    if (fNeedClose) {
      if (kLine_Verb == this->autoClose(ptsParam)) {
        return kLine_Verb;
      }
      fNeedClose = false;
      return kClose_Verb;
    }
    return kDone_Verb;
  }

  unsigned verb = *fVerbs++;
  const SkPoint* PK_RESTRICT srcPts = fPts;
  SkPoint* PK_RESTRICT pts = ptsParam;

  switch (verb) {
    case kMove_Verb:
      if (fNeedClose) {
        fVerbs--;  // move back one verb
        verb = this->autoClose(pts);
        if (verb == kClose_Verb) {
          fNeedClose = false;
        }
        return (Verb)verb;
      }
      if (fVerbs == fVerbStop) {  // might be a trailing moveto
        return kDone_Verb;
      }
      fMoveTo = *srcPts;
      pts[0] = *srcPts;
      srcPts += 1;
      fLastPt = fMoveTo;
      fNeedClose = fForceClose;
      break;
    case kLine_Verb:
      pts[0] = fLastPt;
      pts[1] = srcPts[0];
      fLastPt = srcPts[0];
      fCloseLine = false;
      srcPts += 1;
      break;
    case kConic_Verb:
      fConicWeights += 1;
      [[fallthrough]];
    case kQuad_Verb:
      pts[0] = fLastPt;
      memcpy(&pts[1], srcPts, 2 * sizeof(SkPoint));
      fLastPt = srcPts[1];
      srcPts += 2;
      break;
    case kCubic_Verb:
      pts[0] = fLastPt;
      memcpy(&pts[1], srcPts, 3 * sizeof(SkPoint));
      fLastPt = srcPts[2];
      srcPts += 3;
      break;
    case kClose_Verb:
      verb = this->autoClose(pts);
      if (verb == kLine_Verb) {
        fVerbs--;  // move back one verb
      } else {
        fNeedClose = false;
      }
      fLastPt = fMoveTo;
      break;
  }
  fPts = srcPts;
  return (Verb)verb;
}

void SkPath::RawIter::setPath(const SkPath& path) {
  SkPathPriv::Iterate iterate(path);
  fIter = iterate.begin();
  fEnd = iterate.end();
}

SkPath::Verb SkPath::RawIter::next(SkPoint pts[4]) {
  if (!(fIter != fEnd)) {
    return kDone_Verb;
  }
  auto verb = std::get<0>(*fIter);
  auto iterPts = std::get<1>(*fIter);
  auto weights = std::get<2>(*fIter);
  int numPts;
  switch (verb) {
    case SkPathVerb::kMove:
      numPts = 1;
      break;
    case SkPathVerb::kLine:
      numPts = 2;
      break;
    case SkPathVerb::kQuad:
      numPts = 3;
      break;
    case SkPathVerb::kConic:
      numPts = 3;
      fConicWeight = *weights;
      break;
    case SkPathVerb::kCubic:
      numPts = 4;
      break;
    case SkPathVerb::kClose:
      numPts = 0;
      break;
  }
  memcpy(pts, iterPts, sizeof(SkPoint) * numPts);
  ++fIter;
  return (Verb)verb;
}

static int sign(SkScalar x) {
  return x < 0;
}

#define kValueNeverReturnedBySign 2

enum DirChange {
  kUnknown_DirChange,
  kLeft_DirChange,
  kRight_DirChange,
  kStraight_DirChange,
  kBackwards_DirChange,  // if double back, allow simple lines to be convex
  kInvalid_DirChange
};

// only valid for a single contour
struct Convexicator {
  /** The direction returned is only valid if the path is determined convex */
  SkPathFirstDirection getFirstDirection() const {
    return fFirstDirection;
  }

  void setMovePt(const SkPoint& pt) {
    fFirstPt = fLastPt = pt;
    fExpectedDir = kInvalid_DirChange;
  }

  bool addPt(const SkPoint& pt) {
    if (fLastPt == pt) {
      return true;
    }
    // should only be true for first non-zero vector after setMovePt was called.
    if (fFirstPt == fLastPt && fExpectedDir == kInvalid_DirChange) {
      fLastVec = pt - fLastPt;
      fFirstVec = fLastVec;
    } else if (!this->addVec(pt - fLastPt)) {
      return false;
    }
    fLastPt = pt;
    return true;
  }

  static SkPathConvexity BySign(const SkPoint points[], int count) {
    if (count <= 3) {
      // point, line, or triangle are always convex
      return SkPathConvexity::kConvex;
    }

    const SkPoint* last = points + count;
    SkPoint currPt = *points++;
    SkPoint firstPt = currPt;
    int dxes = 0;
    int dyes = 0;
    int lastSx = kValueNeverReturnedBySign;
    int lastSy = kValueNeverReturnedBySign;
    for (int outerLoop = 0; outerLoop < 2; ++outerLoop) {
      while (points != last) {
        SkVector vec = *points - currPt;
        if (!vec.isZero()) {
          // give up if vector construction failed
          if (!vec.isFinite()) {
            return SkPathConvexity::kUnknown;
          }
          int sx = sign(vec.fX);
          int sy = sign(vec.fY);
          dxes += (sx != lastSx);
          dyes += (sy != lastSy);
          if (dxes > 3 || dyes > 3) {
            return SkPathConvexity::kConcave;
          }
          lastSx = sx;
          lastSy = sy;
        }
        currPt = *points++;
        if (outerLoop) {
          break;
        }
      }
      points = &firstPt;
    }
    return SkPathConvexity::kConvex;  // that is, it may be convex, don't know yet
  }

  bool close() {
    // If this was an explicit close, there was already a lineTo to fFirstPoint, so this
    // addPt() is a no-op. Otherwise, the addPt implicitly closes the contour. In either case,
    // we have to check the direction change along the first vector in case it is concave.
    return this->addPt(fFirstPt) && this->addVec(fFirstVec);
  }

  bool isFinite() const {
    return fIsFinite;
  }

  int reversals() const {
    return fReversals;
  }

 private:
  DirChange directionChange(const SkVector& curVec) {
    SkScalar cross = SkPoint::CrossProduct(fLastVec, curVec);
    if (!SkScalarIsFinite(cross)) {
      return kUnknown_DirChange;
    }
    if (cross == 0) {
      return fLastVec.dot(curVec) < 0 ? kBackwards_DirChange : kStraight_DirChange;
    }
    return 1 == SkScalarSignAsInt(cross) ? kRight_DirChange : kLeft_DirChange;
  }

  bool addVec(const SkVector& curVec) {
    DirChange dir = this->directionChange(curVec);
    switch (dir) {
      case kLeft_DirChange:  // fall through
      case kRight_DirChange:
        if (kInvalid_DirChange == fExpectedDir) {
          fExpectedDir = dir;
          fFirstDirection =
              (kRight_DirChange == dir) ? SkPathFirstDirection::kCW : SkPathFirstDirection::kCCW;
        } else if (dir != fExpectedDir) {
          fFirstDirection = SkPathFirstDirection::kUnknown;
          return false;
        }
        fLastVec = curVec;
        break;
      case kStraight_DirChange:
        break;
      case kBackwards_DirChange:
        //  allow path to reverse direction twice
        //    Given path.moveTo(0, 0); path.lineTo(1, 1);
        //    - 1st reversal: direction change formed by line (0,0 1,1), line (1,1 0,0)
        //    - 2nd reversal: direction change formed by line (1,1 0,0), line (0,0 1,1)
        fLastVec = curVec;
        return ++fReversals < 3;
      case kUnknown_DirChange:
        return (fIsFinite = false);
      case kInvalid_DirChange:
        PK_ABORT("Use of invalid direction change flag");
        break;
    }
    return true;
  }

  SkPoint fFirstPt{0, 0};    // The first point of the contour, e.g. moveTo(x,y)
  SkVector fFirstVec{0, 0};  // The direction leaving fFirstPt to the next vertex

  SkPoint fLastPt{0, 0};    // The last point passed to addPt()
  SkVector fLastVec{0, 0};  // The direction that brought the path to fLastPt

  DirChange fExpectedDir{kInvalid_DirChange};
  SkPathFirstDirection fFirstDirection{SkPathFirstDirection::kUnknown};
  int fReversals{0};
  bool fIsFinite{true};
};

SkPathConvexity SkPath::computeConvexity() const {
  auto setComputedConvexity = [=](SkPathConvexity convexity) {
    this->setConvexity(convexity);
    return convexity;
  };

  auto setFail = [=]() { return setComputedConvexity(SkPathConvexity::kConcave); };

  if (!this->isFinite()) {
    return setFail();
  }

  // pointCount potentially includes a block of leading moveTos and trailing moveTos. Convexity
  // only cares about the last of the initial moveTos and the verbs before the final moveTos.
  int pointCount = this->countPoints();
  int skipCount = SkPathPriv::LeadingMoveToCount(*this) - 1;

  if (fLastMoveToIndex >= 0) {
    if (fLastMoveToIndex == pointCount - 1) {
      // Find the last real verb that affects convexity
      auto verbs = fPathRef->verbsEnd() - 1;
      while (verbs > fPathRef->verbsBegin() && *verbs == Verb::kMove_Verb) {
        verbs--;
        pointCount--;
      }
    } else if (fLastMoveToIndex != skipCount) {
      // There's an additional moveTo between two blocks of other verbs, so the path must have
      // more than one contour and cannot be convex.
      return setComputedConvexity(SkPathConvexity::kConcave);
    }  // else no trailing or intermediate moveTos to worry about
  }
  const SkPoint* points = fPathRef->points();
  if (skipCount > 0) {
    points += skipCount;
    pointCount -= skipCount;
  }

  // Check to see if path changes direction more than three times as quick concave test
  SkPathConvexity convexity = Convexicator::BySign(points, pointCount);
  if (SkPathConvexity::kConvex != convexity) {
    return setComputedConvexity(SkPathConvexity::kConcave);
  }

  int contourCount = 0;
  bool needsClose = false;
  Convexicator state;

  for (auto iter: SkPathPriv::Iterate(*this)) {
    auto verb = std::get<0>(iter);
    auto pts = std::get<1>(iter);
    auto wt = std::get<2>(iter);
    // Looking for the last moveTo before non-move verbs start
    if (contourCount == 0) {
      if (verb == SkPathVerb::kMove) {
        state.setMovePt(pts[0]);
      } else {
        // Starting the actual contour, fall through to c=1 to add the points
        contourCount++;
        needsClose = true;
      }
    }
    // Accumulating points into the Convexicator until we hit a close or another move
    if (contourCount == 1) {
      if (verb == SkPathVerb::kClose || verb == SkPathVerb::kMove) {
        if (!state.close()) {
          return setFail();
        }
        needsClose = false;
        contourCount++;
      } else {
        // lines add 1 point, cubics add 3, conics and quads add 2
        int count = SkPathPriv::PtsInVerb((unsigned)verb);
        for (int i = 1; i <= count; ++i) {
          if (!state.addPt(pts[i])) {
            return setFail();
          }
        }
      }
    } else {
      // The first contour has closed and anything other than spurious trailing moves means
      // there's multiple contours and the path can't be convex
      if (verb != SkPathVerb::kMove) {
        return setFail();
      }
    }
  }

  // If the path isn't explicitly closed do so implicitly
  if (needsClose && !state.close()) {
    return setFail();
  }

  if (this->getFirstDirection() == SkPathFirstDirection::kUnknown) {
    if (state.getFirstDirection() == SkPathFirstDirection::kUnknown &&
        !this->getBounds().isEmpty()) {
      return setComputedConvexity(state.reversals() < 3 ? SkPathConvexity::kConvex
                                                        : SkPathConvexity::kConcave);
    }
    this->setFirstDirection(state.getFirstDirection());
  }
  return setComputedConvexity(SkPathConvexity::kConvex);
}

///////////////////////////////////////////////////////////////////////////////

class ContourIter {
 public:
  ContourIter(const SkPathRef& pathRef);

  bool done() const {
    return fDone;
  }

  // if !done() then these may be called
  int count() const {
    return fCurrPtCount;
  }

  const SkPoint* pts() const {
    return fCurrPt;
  }

  void next();

 private:
  int fCurrPtCount;
  const SkPoint* fCurrPt;
  const uint8_t* fCurrVerb;
  const uint8_t* fStopVerbs;
  const SkScalar* fCurrConicWeight;
  bool fDone;
};

ContourIter::ContourIter(const SkPathRef& pathRef) {
  fStopVerbs = pathRef.verbsEnd();
  fDone = false;
  fCurrPt = pathRef.points();
  fCurrVerb = pathRef.verbsBegin();
  fCurrConicWeight = pathRef.conicWeights();
  fCurrPtCount = 0;
  this->next();
}

void ContourIter::next() {
  if (fCurrVerb >= fStopVerbs) {
    fDone = true;
  }
  if (fDone) {
    return;
  }

  // skip pts of prev contour
  fCurrPt += fCurrPtCount;

  int ptCount = 1;  // moveTo
  const uint8_t* verbs = fCurrVerb;

  for (verbs++; verbs < fStopVerbs; verbs++) {
    switch (*verbs) {
      case SkPath::kMove_Verb:
        goto CONTOUR_END;
      case SkPath::kLine_Verb:
        ptCount += 1;
        break;
      case SkPath::kConic_Verb:
        fCurrConicWeight += 1;
        [[fallthrough]];
      case SkPath::kQuad_Verb:
        ptCount += 2;
        break;
      case SkPath::kCubic_Verb:
        ptCount += 3;
        break;
      case SkPath::kClose_Verb:
        break;
      default:
        PkDEBUGFAIL("unexpected verb");
        break;
    }
  }
  CONTOUR_END:
  fCurrPtCount = ptCount;
  fCurrVerb = verbs;
}

// returns cross product of (p1 - p0) and (p2 - p0)
static SkScalar cross_prod(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2) {
  SkScalar cross = SkPoint::CrossProduct(p1 - p0, p2 - p0);
  // We may get 0 when the above subtracts underflow. We expect this to be
  // very rare and lazily promote to double.
  if (0 == cross) {
    double p0x = PkScalarToDouble(p0.fX);
    double p0y = PkScalarToDouble(p0.fY);

    double p1x = PkScalarToDouble(p1.fX);
    double p1y = PkScalarToDouble(p1.fY);

    double p2x = PkScalarToDouble(p2.fX);
    double p2y = PkScalarToDouble(p2.fY);

    cross = PkDoubleToScalar((p1x - p0x) * (p2y - p0y) - (p1y - p0y) * (p2x - p0x));
  }
  return cross;
}

// Returns the first pt with the maximum Y coordinate
static int find_max_y(const SkPoint pts[], int count) {
  SkScalar max = pts[0].fY;
  int firstIndex = 0;
  for (int i = 1; i < count; ++i) {
    SkScalar y = pts[i].fY;
    if (y > max) {
      max = y;
      firstIndex = i;
    }
  }
  return firstIndex;
}

static int find_diff_pt(const SkPoint pts[], int index, int n, int inc) {
  int i = index;
  for (;;) {
    i = (i + inc) % n;
    if (i == index) {  // we wrapped around, so abort
      break;
    }
    if (pts[index] != pts[i]) {  // found a different point, success!
      break;
    }
  }
  return i;
}

/**
 *  Starting at index, and moving forward (incrementing), find the xmin and
 *  xmax of the contiguous points that have the same Y.
 */
static int find_min_max_x_at_y(const SkPoint pts[], int index, int n, int* maxIndexPtr) {
  const SkScalar y = pts[index].fY;
  SkScalar min = pts[index].fX;
  SkScalar max = min;
  int minIndex = index;
  int maxIndex = index;
  for (int i = index + 1; i < n; ++i) {
    if (pts[i].fY != y) {
      break;
    }
    SkScalar x = pts[i].fX;
    if (x < min) {
      min = x;
      minIndex = i;
    } else if (x > max) {
      max = x;
      maxIndex = i;
    }
  }
  *maxIndexPtr = maxIndex;
  return minIndex;
}

static SkPathFirstDirection crossToDir(SkScalar cross) {
  return cross > 0 ? SkPathFirstDirection::kCW : SkPathFirstDirection::kCCW;
}

/*
 *  We loop through all contours, and keep the computed cross-product of the
 *  contour that contained the global y-max. If we just look at the first
 *  contour, we may find one that is wound the opposite way (correctly) since
 *  it is the interior of a hole (e.g. 'o'). Thus we must find the contour
 *  that is outer most (or at least has the global y-max) before we can consider
 *  its cross product.
 */
SkPathFirstDirection SkPathPriv::ComputeFirstDirection(const SkPath& path) {
  auto d = path.getFirstDirection();
  if (d != SkPathFirstDirection::kUnknown) {
    return d;
  }

  // We don't want to pay the cost for computing convexity if it is unknown,
  // so we call getConvexityOrUnknown() instead of isConvex().
  if (path.getConvexityOrUnknown() == SkPathConvexity::kConvex) {
    return d;
  }

  ContourIter iter(*path.fPathRef);

  // initialize with our logical y-min
  SkScalar ymax = path.getBounds().fTop;
  SkScalar ymaxCross = 0;

  for (; !iter.done(); iter.next()) {
    int n = iter.count();
    if (n < 3) {
      continue;
    }

    const SkPoint* pts = iter.pts();
    SkScalar cross = 0;
    int index = find_max_y(pts, n);
    if (pts[index].fY < ymax) {
      continue;
    }

    // If there is more than 1 distinct point at the y-max, we take the
    // x-min and x-max of them and just subtract to compute the dir.
    if (pts[(index + 1) % n].fY == pts[index].fY) {
      int maxIndex;
      int minIndex = find_min_max_x_at_y(pts, index, n, &maxIndex);
      if (minIndex == maxIndex) {
        goto TRY_CROSSPROD;
      }
      // we just subtract the indices, and let that auto-convert to
      // SkScalar, since we just want - or + to signal the direction.
      cross = minIndex - maxIndex;
    } else {
      TRY_CROSSPROD:
      // Find a next and prev index to use for the cross-product test,
      // but we try to find pts that form non-zero vectors from pts[index]
      //
      // Its possible that we can't find two non-degenerate vectors, so
      // we have to guard our search (e.g. all the pts could be in the
      // same place).

      // we pass n - 1 instead of -1 so we don't foul up % operator by
      // passing it a negative LH argument.
      int prev = find_diff_pt(pts, index, n, n - 1);
      if (prev == index) {
        // completely degenerate, skip to next contour
        continue;
      }
      int next = find_diff_pt(pts, index, n, 1);
      cross = cross_prod(pts[prev], pts[index], pts[next]);
      // if we get a zero and the points are horizontal, then we look at the spread in
      // x-direction. We really should continue to walk away from the degeneracy until
      // there is a divergence.
      if (0 == cross && pts[prev].fY == pts[index].fY && pts[next].fY == pts[index].fY) {
        // construct the subtract so we get the correct Direction below
        cross = pts[index].fX - pts[next].fX;
      }
    }

    if (cross) {
      // record our best guess so far
      ymax = pts[index].fY;
      ymaxCross = cross;
    }
  }
  if (ymaxCross) {
    d = crossToDir(ymaxCross);
    path.setFirstDirection(d);
  }
  return d;  // may still be kUnknown
}

///////////////////////////////////////////////////////////////////////////////

static bool between(SkScalar a, SkScalar b, SkScalar c) {
  return (a - b) * (c - b) <= 0;
}

static SkScalar eval_cubic_pts(SkScalar c0, SkScalar c1, SkScalar c2, SkScalar c3, SkScalar t) {
  SkScalar A = c3 + 3 * (c1 - c2) - c0;
  SkScalar B = 3 * (c2 - c1 - c1 + c0);
  SkScalar C = 3 * (c1 - c0);
  SkScalar D = c0;
  return poly_eval(A, B, C, D, t);
}

template<size_t N>
static void find_minmax(const SkPoint pts[], SkScalar* minPtr, SkScalar* maxPtr) {
  SkScalar min, max;
  min = max = pts[0].fX;
  for (size_t i = 1; i < N; ++i) {
    min = std::min(min, pts[i].fX);
    max = std::max(max, pts[i].fX);
  }
  *minPtr = min;
  *maxPtr = max;
}

static bool checkOnCurve(SkScalar x, SkScalar y, const SkPoint& start, const SkPoint& end) {
  if (start.fY == end.fY) {
    return between(start.fX, x, end.fX) && x != end.fX;
  } else {
    return x == start.fX && y == start.fY;
  }
}

static int winding_mono_cubic(const SkPoint pts[], SkScalar x, SkScalar y, int* onCurveCount) {
  SkScalar y0 = pts[0].fY;
  SkScalar y3 = pts[3].fY;

  int dir = 1;
  if (y0 > y3) {
    using std::swap;
    swap(y0, y3);
    dir = -1;
  }
  if (y < y0 || y > y3) {
    return 0;
  }
  if (checkOnCurve(x, y, pts[0], pts[3])) {
    *onCurveCount += 1;
    return 0;
  }
  if (y == y3) {
    return 0;
  }

  // quickreject or quickaccept
  SkScalar min, max;
  find_minmax<4>(pts, &min, &max);
  if (x < min) {
    return 0;
  }
  if (x > max) {
    return dir;
  }

  // compute the actual x(t) value
  SkScalar t;
  if (!SkCubicClipper::ChopMonoAtY(pts, y, &t)) {
    return 0;
  }
  SkScalar xt = eval_cubic_pts(pts[0].fX, pts[1].fX, pts[2].fX, pts[3].fX, t);
  if (SkScalarNearlyEqual(xt, x)) {
    if (x != pts[3].fX || y != pts[3].fY) {  // don't test end points; they're start points
      *onCurveCount += 1;
      return 0;
    }
  }
  return xt < x ? dir : 0;
}

static int winding_cubic(const SkPoint pts[], SkScalar x, SkScalar y, int* onCurveCount) {
  SkPoint dst[10];
  int n = SkChopCubicAtYExtrema(pts, dst);
  int w = 0;
  for (int i = 0; i <= n; ++i) {
    w += winding_mono_cubic(&dst[i * 3], x, y, onCurveCount);
  }
  return w;
}

static double conic_eval_numerator(const SkScalar src[], SkScalar w, SkScalar t) {
  SkScalar src2w = src[2] * w;
  SkScalar C = src[0];
  SkScalar A = src[4] - 2 * src2w + C;
  SkScalar B = 2 * (src2w - C);
  return poly_eval(A, B, C, t);
}

static double conic_eval_denominator(SkScalar w, SkScalar t) {
  SkScalar B = 2 * (w - 1);
  SkScalar C = 1;
  SkScalar A = -B;
  return poly_eval(A, B, C, t);
}

static int winding_mono_conic(const SkConic& conic, SkScalar x, SkScalar y, int* onCurveCount) {
  const SkPoint* pts = conic.fPts;
  SkScalar y0 = pts[0].fY;
  SkScalar y2 = pts[2].fY;

  int dir = 1;
  if (y0 > y2) {
    using std::swap;
    swap(y0, y2);
    dir = -1;
  }
  if (y < y0 || y > y2) {
    return 0;
  }
  if (checkOnCurve(x, y, pts[0], pts[2])) {
    *onCurveCount += 1;
    return 0;
  }
  if (y == y2) {
    return 0;
  }

  SkScalar roots[2];
  SkScalar A = pts[2].fY;
  SkScalar B = pts[1].fY * conic.fW - y * conic.fW + y;
  SkScalar C = pts[0].fY;
  A += C - 2 * B;  // A = a + c - 2*(b*w - yCept*w + yCept)
  B -= C;          // B = b*w - w * yCept + yCept - a
  C -= y;
  int n = SkFindUnitQuadRoots(A, 2 * B, C, roots);
  SkScalar xt;
  if (0 == n) {
    // zero roots are returned only when y0 == y
    // Need [0] if dir == 1
    // and  [2] if dir == -1
    xt = pts[1 - dir].fX;
  } else {
    SkScalar t = roots[0];
    xt = conic_eval_numerator(&pts[0].fX, conic.fW, t) / conic_eval_denominator(conic.fW, t);
  }
  if (SkScalarNearlyEqual(xt, x)) {
    if (x != pts[2].fX || y != pts[2].fY) {  // don't test end points; they're start points
      *onCurveCount += 1;
      return 0;
    }
  }
  return xt < x ? dir : 0;
}

static bool is_mono_quad(SkScalar y0, SkScalar y1, SkScalar y2) {
  //    return SkScalarSignAsInt(y0 - y1) + SkScalarSignAsInt(y1 - y2) != 0;
  if (y0 == y1) {
    return true;
  }
  if (y0 < y1) {
    return y1 <= y2;
  } else {
    return y1 >= y2;
  }
}

static int winding_conic(const SkPoint pts[], SkScalar x, SkScalar y, SkScalar weight,
                         int* onCurveCount) {
  SkConic conic(pts, weight);
  SkConic chopped[2];
  // If the data points are very large, the conic may not be monotonic but may also
  // fail to chop. Then, the chopper does not split the original conic in two.
  bool isMono = is_mono_quad(pts[0].fY, pts[1].fY, pts[2].fY) || !conic.chopAtYExtrema(chopped);
  int w = winding_mono_conic(isMono ? conic : chopped[0], x, y, onCurveCount);
  if (!isMono) {
    w += winding_mono_conic(chopped[1], x, y, onCurveCount);
  }
  return w;
}

static int winding_mono_quad(const SkPoint pts[], SkScalar x, SkScalar y, int* onCurveCount) {
  SkScalar y0 = pts[0].fY;
  SkScalar y2 = pts[2].fY;

  int dir = 1;
  if (y0 > y2) {
    using std::swap;
    swap(y0, y2);
    dir = -1;
  }
  if (y < y0 || y > y2) {
    return 0;
  }
  if (checkOnCurve(x, y, pts[0], pts[2])) {
    *onCurveCount += 1;
    return 0;
  }
  if (y == y2) {
    return 0;
  }
  // bounds check on X (not required. is it faster?)
#if 0
  if (pts[0].fX > x && pts[1].fX > x && pts[2].fX > x) {
      return 0;
  }
#endif

  SkScalar roots[2];
  int n = SkFindUnitQuadRoots(pts[0].fY - 2 * pts[1].fY + pts[2].fY, 2 * (pts[1].fY - pts[0].fY),
                              pts[0].fY - y, roots);
  SkScalar xt;
  if (0 == n) {
    // zero roots are returned only when y0 == y
    // Need [0] if dir == 1
    // and  [2] if dir == -1
    xt = pts[1 - dir].fX;
  } else {
    SkScalar t = roots[0];
    SkScalar C = pts[0].fX;
    SkScalar A = pts[2].fX - 2 * pts[1].fX + C;
    SkScalar B = 2 * (pts[1].fX - C);
    xt = poly_eval(A, B, C, t);
  }
  if (SkScalarNearlyEqual(xt, x)) {
    if (x != pts[2].fX || y != pts[2].fY) {  // don't test end points; they're start points
      *onCurveCount += 1;
      return 0;
    }
  }
  return xt < x ? dir : 0;
}

static int winding_quad(const SkPoint pts[], SkScalar x, SkScalar y, int* onCurveCount) {
  SkPoint dst[5];
  int n = 0;

  if (!is_mono_quad(pts[0].fY, pts[1].fY, pts[2].fY)) {
    n = SkChopQuadAtYExtrema(pts, dst);
    pts = dst;
  }
  int w = winding_mono_quad(pts, x, y, onCurveCount);
  if (n > 0) {
    w += winding_mono_quad(&pts[2], x, y, onCurveCount);
  }
  return w;
}

static int winding_line(const SkPoint pts[], SkScalar x, SkScalar y, int* onCurveCount) {
  SkScalar x0 = pts[0].fX;
  SkScalar y0 = pts[0].fY;
  SkScalar x1 = pts[1].fX;
  SkScalar y1 = pts[1].fY;

  SkScalar dy = y1 - y0;

  int dir = 1;
  if (y0 > y1) {
    using std::swap;
    swap(y0, y1);
    dir = -1;
  }
  if (y < y0 || y > y1) {
    return 0;
  }
  if (checkOnCurve(x, y, pts[0], pts[1])) {
    *onCurveCount += 1;
    return 0;
  }
  if (y == y1) {
    return 0;
  }
  SkScalar cross = (x1 - x0) * (y - pts[0].fY) - dy * (x - x0);

  if (!cross) {
    // zero cross means the point is on the line, and since the case where
    // y of the query point is at the end point is handled above, we can be
    // sure that we're on the line (excluding the end point) here
    if (x != x1 || y != pts[1].fY) {
      *onCurveCount += 1;
    }
    dir = 0;
  } else if (SkScalarSignAsInt(cross) == dir) {
    dir = 0;
  }
  return dir;
}

static void tangent_cubic(const SkPoint pts[], SkScalar x, SkScalar y,
                          SkTDArray<SkVector>* tangents) {
  if (!between(pts[0].fY, y, pts[1].fY) && !between(pts[1].fY, y, pts[2].fY) &&
      !between(pts[2].fY, y, pts[3].fY)) {
    return;
  }
  if (!between(pts[0].fX, x, pts[1].fX) && !between(pts[1].fX, x, pts[2].fX) &&
      !between(pts[2].fX, x, pts[3].fX)) {
    return;
  }
  SkPoint dst[10];
  int n = SkChopCubicAtYExtrema(pts, dst);
  for (int i = 0; i <= n; ++i) {
    SkPoint* c = &dst[i * 3];
    SkScalar t;
    if (!SkCubicClipper::ChopMonoAtY(c, y, &t)) {
      continue;
    }
    SkScalar xt = eval_cubic_pts(c[0].fX, c[1].fX, c[2].fX, c[3].fX, t);
    if (!SkScalarNearlyEqual(x, xt)) {
      continue;
    }
    SkVector tangent;
    SkEvalCubicAt(c, t, nullptr, &tangent, nullptr);
    tangents->push_back(tangent);
  }
}

static void tangent_conic(const SkPoint pts[], SkScalar x, SkScalar y, SkScalar w,
                          SkTDArray<SkVector>* tangents) {
  if (!between(pts[0].fY, y, pts[1].fY) && !between(pts[1].fY, y, pts[2].fY)) {
    return;
  }
  if (!between(pts[0].fX, x, pts[1].fX) && !between(pts[1].fX, x, pts[2].fX)) {
    return;
  }
  SkScalar roots[2];
  SkScalar A = pts[2].fY;
  SkScalar B = pts[1].fY * w - y * w + y;
  SkScalar C = pts[0].fY;
  A += C - 2 * B;  // A = a + c - 2*(b*w - yCept*w + yCept)
  B -= C;          // B = b*w - w * yCept + yCept - a
  C -= y;
  int n = SkFindUnitQuadRoots(A, 2 * B, C, roots);
  for (int index = 0; index < n; ++index) {
    SkScalar t = roots[index];
    SkScalar xt = conic_eval_numerator(&pts[0].fX, w, t) / conic_eval_denominator(w, t);
    if (!SkScalarNearlyEqual(x, xt)) {
      continue;
    }
    SkConic conic(pts, w);
    tangents->push_back(conic.evalTangentAt(t));
  }
}

static void tangent_quad(const SkPoint pts[], SkScalar x, SkScalar y,
                         SkTDArray<SkVector>* tangents) {
  if (!between(pts[0].fY, y, pts[1].fY) && !between(pts[1].fY, y, pts[2].fY)) {
    return;
  }
  if (!between(pts[0].fX, x, pts[1].fX) && !between(pts[1].fX, x, pts[2].fX)) {
    return;
  }
  SkScalar roots[2];
  int n = SkFindUnitQuadRoots(pts[0].fY - 2 * pts[1].fY + pts[2].fY, 2 * (pts[1].fY - pts[0].fY),
                              pts[0].fY - y, roots);
  for (int index = 0; index < n; ++index) {
    SkScalar t = roots[index];
    SkScalar C = pts[0].fX;
    SkScalar A = pts[2].fX - 2 * pts[1].fX + C;
    SkScalar B = 2 * (pts[1].fX - C);
    SkScalar xt = poly_eval(A, B, C, t);
    if (!SkScalarNearlyEqual(x, xt)) {
      continue;
    }
    tangents->push_back(SkEvalQuadTangentAt(pts, t));
  }
}

static void tangent_line(const SkPoint pts[], SkScalar x, SkScalar y,
                         SkTDArray<SkVector>* tangents) {
  SkScalar y0 = pts[0].fY;
  SkScalar y1 = pts[1].fY;
  if (!between(y0, y, y1)) {
    return;
  }
  SkScalar x0 = pts[0].fX;
  SkScalar x1 = pts[1].fX;
  if (!between(x0, x, x1)) {
    return;
  }
  SkScalar dx = x1 - x0;
  SkScalar dy = y1 - y0;
  if (!SkScalarNearlyEqual((x - x0) * dy, dx * (y - y0))) {
    return;
  }
  SkVector v;
  v.set(dx, dy);
  tangents->push_back(v);
}

static bool contains_inclusive(const SkRect& r, SkScalar x, SkScalar y) {
  return r.fLeft <= x && x <= r.fRight && r.fTop <= y && y <= r.fBottom;
}

bool SkPath::contains(SkScalar x, SkScalar y) const {
  bool isInverse = this->isInverseFillType();
  if (this->isEmpty()) {
    return isInverse;
  }

  if (!contains_inclusive(this->getBounds(), x, y)) {
    return isInverse;
  }

  SkPath::Iter iter(*this, true);
  bool done = false;
  int w = 0;
  int onCurveCount = 0;
  do {
    SkPoint pts[4];
    switch (iter.next(pts)) {
      case SkPath::kMove_Verb:
      case SkPath::kClose_Verb:
        break;
      case SkPath::kLine_Verb:
        w += winding_line(pts, x, y, &onCurveCount);
        break;
      case SkPath::kQuad_Verb:
        w += winding_quad(pts, x, y, &onCurveCount);
        break;
      case SkPath::kConic_Verb:
        w += winding_conic(pts, x, y, iter.conicWeight(), &onCurveCount);
        break;
      case SkPath::kCubic_Verb:
        w += winding_cubic(pts, x, y, &onCurveCount);
        break;
      case SkPath::kDone_Verb:
        done = true;
        break;
    }
  } while (!done);
  bool evenOddFill = SkPathFillType::kEvenOdd == this->getFillType() ||
                     SkPathFillType::kInverseEvenOdd == this->getFillType();
  if (evenOddFill) {
    w &= 1;
  }
  if (w) {
    return !isInverse;
  }
  if (onCurveCount <= 1) {
    return SkToBool(onCurveCount) ^ isInverse;
  }
  if ((onCurveCount & 1) || evenOddFill) {
    return SkToBool(onCurveCount & 1) ^ isInverse;
  }
  // If the point touches an even number of curves, and the fill is winding, check for
  // coincidence. Count coincidence as places where the on curve points have identical tangents.
  iter.setPath(*this, true);
  done = false;
  SkTDArray<SkVector> tangents;
  do {
    SkPoint pts[4];
    int oldCount = tangents.count();
    switch (iter.next(pts)) {
      case SkPath::kMove_Verb:
      case SkPath::kClose_Verb:
        break;
      case SkPath::kLine_Verb:
        tangent_line(pts, x, y, &tangents);
        break;
      case SkPath::kQuad_Verb:
        tangent_quad(pts, x, y, &tangents);
        break;
      case SkPath::kConic_Verb:
        tangent_conic(pts, x, y, iter.conicWeight(), &tangents);
        break;
      case SkPath::kCubic_Verb:
        tangent_cubic(pts, x, y, &tangents);
        break;
      case SkPath::kDone_Verb:
        done = true;
        break;
    }
    if (tangents.count() > oldCount) {
      int last = tangents.count() - 1;
      const SkVector& tangent = tangents[last];
      if (SkScalarNearlyZero(SkPointPriv::LengthSqd(tangent))) {
        tangents.remove(last);
      } else {
        for (int index = 0; index < last; ++index) {
          const SkVector& test = tangents[index];
          if (SkScalarNearlyZero(test.cross(tangent)) &&
              SkScalarSignAsInt(tangent.fX * test.fX) <= 0 &&
              SkScalarSignAsInt(tangent.fY * test.fY) <= 0) {
            tangents.remove(last);
            tangents.removeShuffle(index);
            break;
          }
        }
      }
    }
  } while (!done);
  return SkToBool(tangents.count()) ^ isInverse;
}

int SkPath::ConvertConicToQuads(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, SkScalar w,
                                SkPoint pts[], int pow2) {
  const SkConic conic(p0, p1, p2, w);
  return conic.chopIntoQuadsPOW2(pts, pow2);
}

//////////////////////////////////////////////////////////////////////////////////////////////////

bool SkPathPriv::IsRectContour(const SkPath& path, bool allowPartial, int* currVerb,
                               const SkPoint** ptsPtr, bool* isClosed, SkPathDirection* direction,
                               SkRect* rect) {
  int corners = 0;
  SkPoint closeXY;                   // used to determine if final line falls on a diagonal
  SkPoint lineStart;                 // used to construct line from previous point
  const SkPoint* firstPt = nullptr;  // first point in the rect (last of first moves)
  const SkPoint* lastPt = nullptr;   // last point in the rect (last of lines or first if closed)
  SkPoint firstCorner;
  SkPoint thirdCorner;
  const SkPoint* pts = *ptsPtr;
  const SkPoint* savePts = nullptr;  // used to allow caller to iterate through a pair of rects
  lineStart.set(0, 0);
  signed char directions[] = {-1, -1, -1, -1, -1};  // -1 to 3; -1 is uninitialized
  bool closedOrMoved = false;
  bool autoClose = false;
  bool insertClose = false;
  int verbCnt = path.fPathRef->countVerbs();
  while (*currVerb < verbCnt && (!allowPartial || !autoClose)) {
    uint8_t verb = insertClose ? (uint8_t)SkPath::kClose_Verb : path.fPathRef->atVerb(*currVerb);
    switch (verb) {
      case SkPath::kClose_Verb:
        savePts = pts;
        autoClose = true;
        insertClose = false;
        [[fallthrough]];
      case SkPath::kLine_Verb: {
        if (SkPath::kClose_Verb != verb) {
          lastPt = pts;
        }
        SkPoint lineEnd = SkPath::kClose_Verb == verb ? *firstPt : *pts++;
        SkVector lineDelta = lineEnd - lineStart;
        if (lineDelta.fX && lineDelta.fY) {
          return false;  // diagonal
        }
        if (!lineDelta.isFinite()) {
          return false;  // path contains infinity or NaN
        }
        if (lineStart == lineEnd) {
          break;  // single point on side OK
        }
        int nextDirection = rect_make_dir(lineDelta.fX, lineDelta.fY);  // 0 to 3
        if (0 == corners) {
          directions[0] = nextDirection;
          corners = 1;
          closedOrMoved = false;
          lineStart = lineEnd;
          break;
        }
        if (closedOrMoved) {
          return false;  // closed followed by a line
        }
        if (autoClose && nextDirection == directions[0]) {
          break;  // colinear with first
        }
        closedOrMoved = autoClose;
        if (directions[corners - 1] == nextDirection) {
          if (3 == corners && SkPath::kLine_Verb == verb) {
            thirdCorner = lineEnd;
          }
          lineStart = lineEnd;
          break;  // colinear segment
        }
        directions[corners++] = nextDirection;
        // opposite lines must point in opposite directions; xoring them should equal 2
        switch (corners) {
          case 2:
            firstCorner = lineStart;
            break;
          case 3:
            if ((directions[0] ^ directions[2]) != 2) {
              return false;
            }
            thirdCorner = lineEnd;
            break;
          case 4:
            if ((directions[1] ^ directions[3]) != 2) {
              return false;
            }
            break;
          default:
            return false;  // too many direction changes
        }
        lineStart = lineEnd;
        break;
      }
      case SkPath::kQuad_Verb:
      case SkPath::kConic_Verb:
      case SkPath::kCubic_Verb:
        return false;  // quadratic, cubic not allowed
      case SkPath::kMove_Verb:
        if (allowPartial && !autoClose && directions[0] >= 0) {
          insertClose = true;
          *currVerb -= 1;  // try move again afterwards
          goto addMissingClose;
        }
        if (!corners) {
          firstPt = pts;
        } else {
          closeXY = *firstPt - *lastPt;
          if (closeXY.fX && closeXY.fY) {
            return false;  // we're diagonal, abort
          }
        }
        lineStart = *pts++;
        closedOrMoved = true;
        break;
      default:
        PkDEBUGFAIL("unexpected verb");
        break;
    }
    *currVerb += 1;
    addMissingClose:;
  }
  // Success if 4 corners and first point equals last
  if (corners < 3 || corners > 4) {
    return false;
  }
  if (savePts) {
    *ptsPtr = savePts;
  }
  // check if close generates diagonal
  closeXY = *firstPt - *lastPt;
  if (closeXY.fX && closeXY.fY) {
    return false;
  }
  if (rect) {
    rect->set(firstCorner, thirdCorner);
  }
  if (isClosed) {
    *isClosed = autoClose;
  }
  if (direction) {
    *direction =
        directions[0] == ((directions[1] + 1) & 3) ? SkPathDirection::kCW : SkPathDirection::kCCW;
  }
  return true;
}

bool SkPathPriv::IsAxisAligned(const SkPath& path) {
  // Conservative (quick) test to see if all segments are axis-aligned.
  // Multiple contours might give a false-negative, but for speed, we ignore that
  // and just look at the raw points.

  const SkPoint* pts = path.fPathRef->points();
  const int count = path.fPathRef->countPoints();

  for (int i = 1; i < count; ++i) {
    if (pts[i - 1].fX != pts[i].fX && pts[i - 1].fY != pts[i].fY) {
      return false;
    }
  }
  return true;
}

int SkPath::toAATriangles(float tolerance,
                          const SkRect& clipBounds,
                          std::vector<float>* vertex) const {
  return GrAATriangulator::PathToAATriangles(*this, tolerance, clipBounds, vertex);
}
}  // namespace pk