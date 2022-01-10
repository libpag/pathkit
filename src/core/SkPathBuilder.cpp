/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/private/SkPathRef.h"
#include "include/private/SkSafe32.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkPathPriv.h"
// need SkDVector
#include "src/pathops/SkPathOpsPoint.h"

namespace pk {
SkPathBuilder::SkPathBuilder() {
    this->reset();
}

SkPathBuilder::~SkPathBuilder() {
}

SkPathBuilder& SkPathBuilder::reset() {
    fPts.reset();
    fVerbs.reset();
    fConicWeights.reset();
    fFillType = SkPathFillType::kWinding;

    // these are internal state

    fSegmentMask = 0;
    fLastMovePoint = {0, 0};
    fLastMoveIndex = -1;        // illegal
    fNeedsMoveVerb = true;

    // testing
    fOverrideConvexity = SkPathConvexity::kUnknown;

    return *this;
}

SkPathBuilder& SkPathBuilder::operator=(const SkPath& src) {
    this->reset().setFillType(src.getFillType());

    for (auto iter : SkPathPriv::Iterate(src)) {
        auto verb = std::get<0>(iter);
        auto pts = std::get<1>(iter);
        auto w = std::get<2>(iter);
        switch (verb) {
            case SkPathVerb::kMove:  this->moveTo(pts[0]); break;
            case SkPathVerb::kLine:  this->lineTo(pts[1]); break;
            case SkPathVerb::kQuad:  this->quadTo(pts[1], pts[2]); break;
            case SkPathVerb::kConic: this->conicTo(pts[1], pts[2], w[0]); break;
            case SkPathVerb::kCubic: this->cubicTo(pts[1], pts[2], pts[3]); break;
            case SkPathVerb::kClose: this->close(); break;
        }
    }
    return *this;
}

void SkPathBuilder::incReserve(int extraPtCount, int extraVbCount) {
    fPts.setReserve(  Sk32_sat_add(fPts.count(),   extraPtCount));
    fVerbs.setReserve(Sk32_sat_add(fVerbs.count(), extraVbCount));
}

/*
 *  Some old behavior in SkPath -- should we keep it?
 *
 *  After each edit (i.e. adding a verb)
        this->setConvexityType(SkPathConvexity::kUnknown);
        this->setFirstDirection(SkPathPriv::kUnknown_FirstDirection);
 */

SkPathBuilder& SkPathBuilder::moveTo(SkPoint pt) {
    // only needed while SkPath is mutable
    fLastMoveIndex = SkToInt(fPts.size());

    fPts.push_back(pt);
    fVerbs.push_back((uint8_t)SkPathVerb::kMove);

    fLastMovePoint = pt;
    fNeedsMoveVerb = false;
    return *this;
}

SkPathBuilder& SkPathBuilder::lineTo(SkPoint pt) {
    this->ensureMove();

    fPts.push_back(pt);
    fVerbs.push_back((uint8_t)SkPathVerb::kLine);

    fSegmentMask |= kLine_SkPathSegmentMask;
    return *this;
}

SkPathBuilder& SkPathBuilder::quadTo(SkPoint pt1, SkPoint pt2) {
    this->ensureMove();

    SkPoint* p = fPts.append(2);
    p[0] = pt1;
    p[1] = pt2;
    fVerbs.push_back((uint8_t)SkPathVerb::kQuad);

    fSegmentMask |= kQuad_SkPathSegmentMask;
    return *this;
}

SkPathBuilder& SkPathBuilder::conicTo(SkPoint pt1, SkPoint pt2, SkScalar w) {
    this->ensureMove();

    SkPoint* p = fPts.append(2);
    p[0] = pt1;
    p[1] = pt2;
    fVerbs.push_back((uint8_t)SkPathVerb::kConic);
    fConicWeights.push_back(w);

    fSegmentMask |= kConic_SkPathSegmentMask;
    return *this;
}

SkPathBuilder& SkPathBuilder::cubicTo(SkPoint pt1, SkPoint pt2, SkPoint pt3) {
    this->ensureMove();

    SkPoint* p = fPts.append(3);
    p[0] = pt1;
    p[1] = pt2;
    p[2] = pt3;
    fVerbs.push_back((uint8_t)SkPathVerb::kCubic);

    fSegmentMask |= kCubic_SkPathSegmentMask;
    return *this;
}

SkPathBuilder& SkPathBuilder::close() {
    if (fVerbs.count() > 0) {
        this->ensureMove();

        fVerbs.push_back((uint8_t)SkPathVerb::kClose);

        // fLastMovePoint stays where it is -- the previous moveTo
        fNeedsMoveVerb = true;
    }
    return *this;
}

SkPath SkPathBuilder::make(sk_sp<SkPathRef> pr) const {
    auto convexity = SkPathConvexity::kUnknown;
    SkPathFirstDirection dir = SkPathFirstDirection::kUnknown;

    switch (fIsA) {
        case kIsA_Oval:
            convexity = SkPathConvexity::kConvex;
            dir = fIsACCW ? SkPathFirstDirection::kCCW : SkPathFirstDirection::kCW;
            break;
        case kIsA_RRect:
            convexity = SkPathConvexity::kConvex;
            dir = fIsACCW ? SkPathFirstDirection::kCCW : SkPathFirstDirection::kCW;
            break;
        default: break;
    }

    if (fOverrideConvexity != SkPathConvexity::kUnknown) {
        convexity = fOverrideConvexity;
    }

    // Wonder if we can combine convexity and dir internally...
    //  unknown, convex_cw, convex_ccw, concave
    // Do we ever have direction w/o convexity, or viceversa (inside path)?
    //
    auto path = SkPath(std::move(pr), fFillType, convexity, dir);

    // This hopefully can go away in the future when Paths are immutable,
    // but if while they are still editable, we need to correctly set this.
    const uint8_t* start = path.fPathRef->verbsBegin();
    const uint8_t* stop  = path.fPathRef->verbsEnd();
    if (start < stop) {
        // peek at the last verb, to know if our last contour is closed
        const bool isClosed = (stop[-1] == (uint8_t)SkPathVerb::kClose);
        path.fLastMoveToIndex = isClosed ? ~fLastMoveIndex : fLastMoveIndex;
    }

    return path;
}

SkPath SkPathBuilder::snapshot() const {
    return this->make(sk_sp<SkPathRef>(new SkPathRef(fPts,
                                                     fVerbs,
                                                     fConicWeights,
                                                     fSegmentMask)));
}

SkPath SkPathBuilder::detach() {
    auto path = this->make(sk_sp<SkPathRef>(new SkPathRef(std::move(fPts),
                                                          std::move(fVerbs),
                                                          std::move(fConicWeights),
                                                          fSegmentMask)));
    this->reset();
    return path;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static bool arc_is_lone_point(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle,
                              SkPoint* pt) {
    if (0 == sweepAngle && (0 == startAngle || PkIntToScalar(360) == startAngle)) {
        // Chrome uses this path to move into and out of ovals. If not
        // treated as a special case the moves can distort the oval's
        // bounding box (and break the circle special case).
        pt->set(oval.fRight, oval.centerY());
        return true;
    } else if (0 == oval.width() && 0 == oval.height()) {
        // Chrome will sometimes create 0 radius round rects. Having degenerate
        // quad segments in the path prevents the path from being recognized as
        // a rect.
        // TODO: optimizing the case where only one of width or height is zero
        // should also be considered. This case, however, doesn't seem to be
        // as common as the single point case.
        pt->set(oval.fRight, oval.fTop);
        return true;
    }
    return false;
}

// Return the unit vectors pointing at the start/stop points for the given start/sweep angles
//
static void angles_to_unit_vectors(SkScalar startAngle, SkScalar sweepAngle,
                                   SkVector* startV, SkVector* stopV, SkRotationDirection* dir) {
    SkScalar startRad = PkDegreesToRadians(startAngle),
             stopRad  = PkDegreesToRadians(startAngle + sweepAngle);

    startV->fY = SkScalarSinSnapToZero(startRad);
    startV->fX = SkScalarCosSnapToZero(startRad);
    stopV->fY = SkScalarSinSnapToZero(stopRad);
    stopV->fX = SkScalarCosSnapToZero(stopRad);

    /*  If the sweep angle is nearly (but less than) 360, then due to precision
     loss in radians-conversion and/or sin/cos, we may end up with coincident
     vectors, which will fool SkBuildQuadArc into doing nothing (bad) instead
     of drawing a nearly complete circle (good).
     e.g. canvas.drawArc(0, 359.99, ...)
     -vs- canvas.drawArc(0, 359.9, ...)
     We try to detect this edge case, and tweak the stop vector
     */
    if (*startV == *stopV) {
        SkScalar sw = PkScalarAbs(sweepAngle);
        if (sw < PkIntToScalar(360) && sw > PkIntToScalar(359)) {
            // make a guess at a tiny angle (in radians) to tweak by
            SkScalar deltaRad = PkScalarCopySign(PK_Scalar1/512, sweepAngle);
            // not sure how much will be enough, so we use a loop
            do {
                stopRad -= deltaRad;
                stopV->fY = SkScalarSinSnapToZero(stopRad);
                stopV->fX = SkScalarCosSnapToZero(stopRad);
            } while (*startV == *stopV);
        }
    }
    *dir = sweepAngle > 0 ? kCW_SkRotationDirection : kCCW_SkRotationDirection;
}

/**
 *  If this returns 0, then the caller should just line-to the singlePt, else it should
 *  ignore singlePt and append the specified number of conics.
 */
static int build_arc_conics(const SkRect& oval, const SkVector& start, const SkVector& stop,
                            SkRotationDirection dir, SkConic conics[SkConic::kMaxConicsForArc],
                            SkPoint* singlePt) {
    SkMatrix    matrix;

    matrix.setScale(PkScalarHalf(oval.width()), PkScalarHalf(oval.height()));
    matrix.postTranslate(oval.centerX(), oval.centerY());

    int count = SkConic::BuildUnitArc(start, stop, dir, &matrix, conics);
    if (0 == count) {
        matrix.mapXY(stop.x(), stop.y(), singlePt);
    }
    return count;
}

namespace {
    template <unsigned N> class PointIterator {
    public:
        PointIterator(SkPathDirection dir, unsigned startIndex)
            : fCurrent(startIndex % N)
            , fAdvance(dir == SkPathDirection::kCW ? 1 : N - 1)
        {}

        const SkPoint& current() const {
            return fPts[fCurrent];
        }

        const SkPoint& next() {
            fCurrent = (fCurrent + fAdvance) % N;
            return this->current();
        }

    protected:
        SkPoint fPts[N];

    private:
        unsigned fCurrent;
        unsigned fAdvance;
    };

    class RectPointIterator : public PointIterator<4> {
    public:
        RectPointIterator(const SkRect& rect, SkPathDirection dir, unsigned startIndex)
        : PointIterator(dir, startIndex) {

            fPts[0] = SkPoint::Make(rect.fLeft, rect.fTop);
            fPts[1] = SkPoint::Make(rect.fRight, rect.fTop);
            fPts[2] = SkPoint::Make(rect.fRight, rect.fBottom);
            fPts[3] = SkPoint::Make(rect.fLeft, rect.fBottom);
        }
    };

    class OvalPointIterator : public PointIterator<4> {
    public:
        OvalPointIterator(const SkRect& oval, SkPathDirection dir, unsigned startIndex)
        : PointIterator(dir, startIndex) {

            const SkScalar cx = oval.centerX();
            const SkScalar cy = oval.centerY();

            fPts[0] = SkPoint::Make(cx, oval.fTop);
            fPts[1] = SkPoint::Make(oval.fRight, cy);
            fPts[2] = SkPoint::Make(cx, oval.fBottom);
            fPts[3] = SkPoint::Make(oval.fLeft, cy);
        }
    };

    class RRectPointIterator : public PointIterator<8> {
    public:
        RRectPointIterator(const SkRRect& rrect, SkPathDirection dir, unsigned startIndex)
            : PointIterator(dir, startIndex)
        {
            const SkRect& bounds = rrect.getBounds();
            const SkScalar L = bounds.fLeft;
            const SkScalar T = bounds.fTop;
            const SkScalar R = bounds.fRight;
            const SkScalar B = bounds.fBottom;

            fPts[0] = SkPoint::Make(L + rrect.radii(SkRRect::kUpperLeft_Corner).fX, T);
            fPts[1] = SkPoint::Make(R - rrect.radii(SkRRect::kUpperRight_Corner).fX, T);
            fPts[2] = SkPoint::Make(R, T + rrect.radii(SkRRect::kUpperRight_Corner).fY);
            fPts[3] = SkPoint::Make(R, B - rrect.radii(SkRRect::kLowerRight_Corner).fY);
            fPts[4] = SkPoint::Make(R - rrect.radii(SkRRect::kLowerRight_Corner).fX, B);
            fPts[5] = SkPoint::Make(L + rrect.radii(SkRRect::kLowerLeft_Corner).fX, B);
            fPts[6] = SkPoint::Make(L, B - rrect.radii(SkRRect::kLowerLeft_Corner).fY);
            fPts[7] = SkPoint::Make(L, T + rrect.radii(SkRRect::kUpperLeft_Corner).fY);
        }
    };
} // anonymous namespace


SkPathBuilder& SkPathBuilder::addRect(const SkRect& rect, SkPathDirection dir, unsigned index) {
    const int kPts   = 4;   // moveTo + 3 lines
    const int kVerbs = 5;   // moveTo + 3 lines + close
    this->incReserve(kPts, kVerbs);

    RectPointIterator iter(rect, dir, index);

    this->moveTo(iter.current());
    this->lineTo(iter.next());
    this->lineTo(iter.next());
    this->lineTo(iter.next());
    return this->close();
}

SkPathBuilder& SkPathBuilder::addOval(const SkRect& oval, SkPathDirection dir, unsigned index) {
    const IsA prevIsA = fIsA;

    const int kPts   = 9;   // moveTo + 4 conics(2 pts each)
    const int kVerbs = 6;   // moveTo + 4 conics + close
    this->incReserve(kPts, kVerbs);

    OvalPointIterator ovalIter(oval, dir, index);
    RectPointIterator rectIter(oval, dir, index + (dir == SkPathDirection::kCW ? 0 : 1));

    // The corner iterator pts are tracking "behind" the oval/radii pts.

    this->moveTo(ovalIter.current());
    for (unsigned i = 0; i < 4; ++i) {
        this->conicTo(rectIter.next(), ovalIter.next(), PK_ScalarRoot2Over2);
    }
    this->close();

    if (prevIsA == kIsA_JustMoves) {
        fIsA      = kIsA_Oval;
        fIsACCW   = (dir == SkPathDirection::kCCW);
        fIsAStart = index % 4;
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addRRect(const SkRRect& rrect, SkPathDirection dir, unsigned index) {
    const IsA prevIsA = fIsA;
    const SkRect& bounds = rrect.getBounds();

    if (rrect.isRect() || rrect.isEmpty()) {
        // degenerate(rect) => radii points are collapsing
        this->addRect(bounds, dir, (index + 1) / 2);
    } else if (rrect.isOval()) {
        // degenerate(oval) => line points are collapsing
        this->addOval(bounds, dir, index / 2);
    } else {
        // we start with a conic on odd indices when moving CW vs. even indices when moving CCW
        const bool startsWithConic = ((index & 1) == (dir == SkPathDirection::kCW));
        const SkScalar weight = PK_ScalarRoot2Over2;

        const int kVerbs = startsWithConic
            ? 9   // moveTo + 4x conicTo + 3x lineTo + close
            : 10; // moveTo + 4x lineTo + 4x conicTo + close
        this->incReserve(kVerbs);

        RRectPointIterator rrectIter(rrect, dir, index);
        // Corner iterator indices follow the collapsed radii model,
        // adjusted such that the start pt is "behind" the radii start pt.
        const unsigned rectStartIndex = index / 2 + (dir == SkPathDirection::kCW ? 0 : 1);
        RectPointIterator rectIter(bounds, dir, rectStartIndex);

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
    }

    if (prevIsA == kIsA_JustMoves) {
        fIsA      = kIsA_RRect;
        fIsACCW   = (dir == SkPathDirection::kCCW);
        fIsAStart = index % 8;
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addCircle(SkScalar x, SkScalar y, SkScalar r, SkPathDirection dir) {
    if (r >= 0) {
        this->addOval(SkRect::MakeLTRB(x - r, y - r, x + r, y + r), dir);
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addPolygon(const SkPoint pts[], int count, bool isClosed) {
    if (count <= 0) {
        return *this;
    }

    this->moveTo(pts[0]);
    this->polylineTo(&pts[1], count - 1);
    if (isClosed) {
        this->close();
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::polylineTo(const SkPoint pts[], int count) {
    if (count > 0) {
        this->ensureMove();

        this->incReserve(count, count);
        memcpy(fPts.append(count), pts, count * sizeof(SkPoint));
        memset(fVerbs.append(count), (uint8_t)SkPathVerb::kLine, count);
        fSegmentMask |= kLine_SkPathSegmentMask;
    }
    return *this;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

SkPathBuilder& SkPathBuilder::offset(SkScalar dx, SkScalar dy) {
    for (auto& p : fPts) {
        p += {dx, dy};
    }
    return *this;
}

SkPathBuilder& SkPathBuilder::addPath(const SkPath& src) {
    SkPath::RawIter iter(src);
    SkPoint pts[4];
    SkPath::Verb verb;

    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kMove_Verb:  this->moveTo (pts[0]); break;
            case SkPath::kLine_Verb:  this->lineTo (pts[1]); break;
            case SkPath::kQuad_Verb:  this->quadTo (pts[1], pts[2]); break;
            case SkPath::kCubic_Verb: this->cubicTo(pts[1], pts[2], pts[3]); break;
            case SkPath::kConic_Verb: this->conicTo(pts[1], pts[2], iter.conicWeight()); break;
            case SkPath::kClose_Verb: this->close(); break;
            case SkPath::kDone_Verb: PkUNREACHABLE;
        }
    }

    return *this;
}

SkPathBuilder& SkPathBuilder::privateReverseAddPath(const SkPath& src) {

    const uint8_t* verbsBegin = src.fPathRef->verbsBegin();
    const uint8_t* verbs = src.fPathRef->verbsEnd();
    const SkPoint* pts = src.fPathRef->pointsEnd();
    const SkScalar* conicWeights = src.fPathRef->conicWeightsEnd();

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
        switch ((SkPathVerb)v) {
            case SkPathVerb::kMove:
                if (needClose) {
                    this->close();
                    needClose = false;
                }
                needMove = true;
                pts += 1;   // so we see the point in "if (needMove)" above
                break;
            case SkPathVerb::kLine:
                this->lineTo(pts[0]);
                break;
            case SkPathVerb::kQuad:
                this->quadTo(pts[1], pts[0]);
                break;
            case SkPathVerb::kConic:
                this->conicTo(pts[1], pts[0], *--conicWeights);
                break;
            case SkPathVerb::kCubic:
                this->cubicTo(pts[2], pts[1], pts[0]);
                break;
            case SkPathVerb::kClose:
                needClose = true;
                break;
            default:
                PkDEBUGFAIL("unexpected verb");
        }
    }
    return *this;
}
}  // namespace pk