/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkPathRef.h"

#include "include/core/SkPath.h"
#include "include/private/SkNx.h"
#include "include/private/SkOnce.h"
#include "include/private/SkTo.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkSafeMath.h"

namespace pk {
//////////////////////////////////////////////////////////////////////////////
SkPathRef::Editor::Editor(sk_sp<SkPathRef>* pathRef,
                          int incReserveVerbs,
                          int incReservePoints)
{
    if ((*pathRef)->unique()) {
        (*pathRef)->incReserve(incReserveVerbs, incReservePoints);
    } else {
        SkPathRef* copy = new SkPathRef;
        copy->copy(**pathRef, incReserveVerbs, incReservePoints);
        pathRef->reset(copy);
    }
    fPathRef = pathRef->get();
    fPathRef->fGenerationID = 0;
    fPathRef->fBoundsIsDirty = true;
}

//////////////////////////////////////////////////////////////////////////////

static SkPathRef* gEmpty = nullptr;

SkPathRef* SkPathRef::CreateEmpty() {
    static SkOnce once;
    once([]{
        gEmpty = new SkPathRef;
        gEmpty->computeBounds();   // Avoids races later to be the first to do this.
    });
    return SkRef(gEmpty);
}

static void transform_dir_and_start(const SkMatrix& matrix, bool isRRect, bool* isCCW,
                                    unsigned* start) {
    int inStart = *start;
    int rm = 0;
    if (isRRect) {
        // Degenerate rrect indices to oval indices and remember the remainder.
        // Ovals have one index per side whereas rrects have two.
        rm = inStart & 0b1;
        inStart /= 2;
    }
    // Is the antidiagonal non-zero (otherwise the diagonal is zero)
    int antiDiag;
    // Is the non-zero value in the top row (either kMScaleX or kMSkewX) negative
    int topNeg;
    // Are the two non-zero diagonal or antidiagonal values the same sign.
    int sameSign;
    if (matrix.get(SkMatrix::kMScaleX) != 0) {
        antiDiag = 0b00;
        if (matrix.get(SkMatrix::kMScaleX) > 0) {
            topNeg = 0b00;
            sameSign = matrix.get(SkMatrix::kMScaleY) > 0 ? 0b01 : 0b00;
        } else {
            topNeg = 0b10;
            sameSign = matrix.get(SkMatrix::kMScaleY) > 0 ? 0b00 : 0b01;
        }
    } else {
        antiDiag = 0b01;
        if (matrix.get(SkMatrix::kMSkewX) > 0) {
            topNeg = 0b00;
            sameSign = matrix.get(SkMatrix::kMSkewY) > 0 ? 0b01 : 0b00;
        } else {
            topNeg = 0b10;
            sameSign = matrix.get(SkMatrix::kMSkewY) > 0 ? 0b00 : 0b01;
        }
    }
    if (sameSign != antiDiag) {
        // This is a rotation (and maybe scale). The direction is unchanged.
        // Trust me on the start computation (or draw yourself some pictures)
        *start = (inStart + 4 - (topNeg | antiDiag)) % 4;
        if (isRRect) {
            *start = 2 * *start + rm;
        }
    } else {
        // This is a mirror (and maybe scale). The direction is reversed.
        *isCCW = !*isCCW;
        // Trust me on the start computation (or draw yourself some pictures)
        *start = (6 + (topNeg | antiDiag) - inStart) % 4;
        if (isRRect) {
            *start = 2 * *start + (rm ? 0 : 1);
        }
    }
}

void SkPathRef::CreateTransformedCopy(sk_sp<SkPathRef>* dst,
                                      const SkPathRef& src,
                                      const SkMatrix& matrix) {
    if (matrix.isIdentity()) {
        if (dst->get() != &src) {
            src.ref();
            dst->reset(const_cast<SkPathRef*>(&src));
        }
        return;
    }

    sk_sp<const SkPathRef> srcKeepAlive;
    if (!(*dst)->unique()) {
        // If dst and src are the same then we are about to drop our only ref on the common path
        // ref. Some other thread may have owned src when we checked unique() above but it may not
        // continue to do so. Add another ref so we continue to be an owner until we're done.
        if (dst->get() == &src) {
            srcKeepAlive.reset(SkRef(&src));
        }
        dst->reset(new SkPathRef);
    }

    if (dst->get() != &src) {
        (*dst)->fVerbs = src.fVerbs;
        (*dst)->fConicWeights = src.fConicWeights;
        (*dst)->fGenerationID = 0;  // mark as dirty
        // don't copy, just allocate the points
        (*dst)->fPoints.setCount(src.fPoints.count());
    }
    matrix.mapPoints((*dst)->fPoints.begin(), src.fPoints.begin(), src.fPoints.count());

    // Need to check this here in case (&src == dst)
    bool canXformBounds = !src.fBoundsIsDirty && matrix.rectStaysRect() && src.countPoints() > 1;

    /*
     *  Here we optimize the bounds computation, by noting if the bounds are
     *  already known, and if so, we just transform those as well and mark
     *  them as "known", rather than force the transformed path to have to
     *  recompute them.
     *
     *  Special gotchas if the path is effectively empty (<= 1 point) or
     *  if it is non-finite. In those cases bounds need to stay empty,
     *  regardless of the matrix.
     */
    if (canXformBounds) {
        (*dst)->fBoundsIsDirty = false;
        if (src.fIsFinite) {
            matrix.mapRect(&(*dst)->fBounds, src.fBounds);
            if (!((*dst)->fIsFinite = (*dst)->fBounds.isFinite())) {
                (*dst)->fBounds.setEmpty();
            }
        } else {
            (*dst)->fIsFinite = false;
            (*dst)->fBounds.setEmpty();
        }
    } else {
        (*dst)->fBoundsIsDirty = true;
    }
    (*dst)->fSegmentMask = src.fSegmentMask;

    // It's an oval only if it stays a rect.
    bool rectStaysRect = matrix.rectStaysRect();
    (*dst)->fIsOval = src.fIsOval && rectStaysRect;
    (*dst)->fIsRRect = src.fIsRRect && rectStaysRect;
    if ((*dst)->fIsOval || (*dst)->fIsRRect) {
        unsigned start = src.fRRectOrOvalStartIdx;
        bool isCCW = SkToBool(src.fRRectOrOvalIsCCW);
        transform_dir_and_start(matrix, (*dst)->fIsRRect, &isCCW, &start);
        (*dst)->fRRectOrOvalIsCCW = isCCW;
        (*dst)->fRRectOrOvalStartIdx = start;
    }

    if (dst->get() == &src) {
        (*dst)->fGenerationID = 0;
    }
}

void SkPathRef::Rewind(sk_sp<SkPathRef>* pathRef) {
    if ((*pathRef)->unique()) {
        (*pathRef)->fBoundsIsDirty = true;  // this also invalidates fIsFinite
        (*pathRef)->fGenerationID = 0;
        (*pathRef)->fPoints.rewind();
        (*pathRef)->fVerbs.rewind();
        (*pathRef)->fConicWeights.rewind();
        (*pathRef)->fSegmentMask = 0;
        (*pathRef)->fIsOval = false;
        (*pathRef)->fIsRRect = false;
    } else {
        int oldVCnt = (*pathRef)->countVerbs();
        int oldPCnt = (*pathRef)->countPoints();
        pathRef->reset(new SkPathRef);
        (*pathRef)->resetToSize(0, 0, 0, oldVCnt, oldPCnt);
    }
}

bool SkPathRef::operator== (const SkPathRef& ref) const {
    // We explicitly check fSegmentMask as a quick-reject. We could skip it,
    // since it is only a cache of info in the fVerbs, but its a fast way to
    // notice a difference
    if (fSegmentMask != ref.fSegmentMask) {
        return false;
    }

    bool genIDMatch = fGenerationID && fGenerationID == ref.fGenerationID;
    if (genIDMatch) {
        return true;
    }
    if (fPoints != ref.fPoints || fConicWeights != ref.fConicWeights || fVerbs != ref.fVerbs) {
        return false;
    }
    if (ref.fVerbs.count() == 0) {
    }
    return true;
}

void SkPathRef::copy(const SkPathRef& ref,
                     int additionalReserveVerbs,
                     int additionalReservePoints) {
    this->resetToSize(ref.fVerbs.count(), ref.fPoints.count(), ref.fConicWeights.count(),
                      additionalReserveVerbs, additionalReservePoints);
    fVerbs = ref.fVerbs;
    fPoints = ref.fPoints;
    fConicWeights = ref.fConicWeights;
    fBoundsIsDirty = ref.fBoundsIsDirty;
    if (!fBoundsIsDirty) {
        fBounds = ref.fBounds;
        fIsFinite = ref.fIsFinite;
    }
    fSegmentMask = ref.fSegmentMask;
    fIsOval = ref.fIsOval;
    fIsRRect = ref.fIsRRect;
    fRRectOrOvalIsCCW = ref.fRRectOrOvalIsCCW;
    fRRectOrOvalStartIdx = ref.fRRectOrOvalStartIdx;
}

void SkPathRef::interpolate(const SkPathRef& ending, SkScalar weight, SkPathRef* out) const {
    const SkScalar* inValues = &ending.getPoints()->fX;
    SkScalar* outValues = &out->getWritablePoints()->fX;
    int count = out->countPoints() * 2;
    for (int index = 0; index < count; ++index) {
        outValues[index] = outValues[index] * weight + inValues[index] * (1 - weight);
    }
    out->fBoundsIsDirty = true;
    out->fIsOval = false;
    out->fIsRRect = false;
}

std::tuple<SkPoint*, SkScalar*> SkPathRef::growForVerbsInPath(const SkPathRef& path) {
    fSegmentMask |= path.fSegmentMask;
    fBoundsIsDirty = true;  // this also invalidates fIsFinite
    fIsOval = false;
    fIsRRect = false;

    if (int numVerbs = path.countVerbs()) {
        memcpy(fVerbs.append(numVerbs), path.fVerbs.begin(), numVerbs * sizeof(fVerbs[0]));
    }

    SkPoint* pts = nullptr;
    if (int numPts = path.countPoints()) {
        pts = fPoints.append(numPts);
    }

    SkScalar* weights = nullptr;
    if (int numConics = path.countWeights()) {
        weights = fConicWeights.append(numConics);
    }

    return {pts, weights};
}

SkPoint* SkPathRef::growForRepeatedVerb(int /*SkPath::Verb*/ verb,
                                        int numVbs,
                                        SkScalar** weights) {
    int pCnt;
    switch (verb) {
        case SkPath::kMove_Verb:
            pCnt = numVbs;
            break;
        case SkPath::kLine_Verb:
            fSegmentMask |= SkPath::kLine_SegmentMask;
            pCnt = numVbs;
            break;
        case SkPath::kQuad_Verb:
            fSegmentMask |= SkPath::kQuad_SegmentMask;
            pCnt = 2 * numVbs;
            break;
        case SkPath::kConic_Verb:
            fSegmentMask |= SkPath::kConic_SegmentMask;
            pCnt = 2 * numVbs;
            break;
        case SkPath::kCubic_Verb:
            fSegmentMask |= SkPath::kCubic_SegmentMask;
            pCnt = 3 * numVbs;
            break;
        default:
            pCnt = 0;
            break;
    }

    fBoundsIsDirty = true;  // this also invalidates fIsFinite
    fIsOval = false;
    fIsRRect = false;

    memset(fVerbs.append(numVbs), verb, numVbs);
    if (SkPath::kConic_Verb == verb) {
        *weights = fConicWeights.append(numVbs);
    }
    SkPoint* pts = fPoints.append(pCnt);

    return pts;
}

SkPoint* SkPathRef::growForVerb(int /* SkPath::Verb*/ verb, SkScalar weight) {
    int pCnt;
    unsigned mask = 0;
    switch (verb) {
        case SkPath::kMove_Verb:
            pCnt = 1;
            break;
        case SkPath::kLine_Verb:
            mask = SkPath::kLine_SegmentMask;
            pCnt = 1;
            break;
        case SkPath::kQuad_Verb:
            mask = SkPath::kQuad_SegmentMask;
            pCnt = 2;
            break;
        case SkPath::kConic_Verb:
            mask = SkPath::kConic_SegmentMask;
            pCnt = 2;
            break;
        case SkPath::kCubic_Verb:
            mask = SkPath::kCubic_SegmentMask;
            pCnt = 3;
            break;
        default:
            pCnt = 0;
            break;
    }

    fSegmentMask |= mask;
    fBoundsIsDirty = true;  // this also invalidates fIsFinite
    fIsOval = false;
    fIsRRect = false;

    *fVerbs.append() = verb;
    if (SkPath::kConic_Verb == verb) {
        *fConicWeights.append() = weight;
    }
    SkPoint* pts = fPoints.append(pCnt);

    return pts;
}

uint32_t SkPathRef::genID() const {
    static const uint32_t kMask = (static_cast<int64_t>(1) << SkPathPriv::kPathRefGenIDBitCnt) - 1;

    if (fGenerationID == 0) {
        if (fPoints.count() == 0 && fVerbs.count() == 0) {
            fGenerationID = kEmptyGenID;
        } else {
            static std::atomic<uint32_t> nextID{kEmptyGenID + 1};
            do {
                fGenerationID = nextID.fetch_add(1, std::memory_order_relaxed) & kMask;
            } while (fGenerationID == 0 || fGenerationID == kEmptyGenID);
        }
    }
    return fGenerationID;
}

SkRRect SkPathRef::getRRect() const {
    const SkRect& bounds = this->getBounds();
    SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    Iter iter(*this);
    SkPoint pts[4];
    uint8_t verb = iter.next(pts);
    PkASSERT(SkPath::kMove_Verb == verb);
    while ((verb = iter.next(pts)) != SkPath::kDone_Verb) {
        if (SkPath::kConic_Verb == verb) {
            SkVector v1_0 = pts[1] - pts[0];
            SkVector v2_1 = pts[2] - pts[1];
            SkVector dxdy;
            if (v1_0.fX) {
                PkASSERT(!v2_1.fX && !v1_0.fY);
                dxdy.set(PkScalarAbs(v1_0.fX), PkScalarAbs(v2_1.fY));
            } else if (!v1_0.fY) {
                PkASSERT(!v2_1.fX || !v2_1.fY);
                dxdy.set(PkScalarAbs(v2_1.fX), PkScalarAbs(v2_1.fY));
            } else {
                PkASSERT(!v2_1.fY);
                dxdy.set(PkScalarAbs(v2_1.fX), PkScalarAbs(v1_0.fY));
            }
            SkRRect::Corner corner =
                    pts[1].fX == bounds.fLeft ?
                        pts[1].fY == bounds.fTop ?
                            SkRRect::kUpperLeft_Corner : SkRRect::kLowerLeft_Corner :
                    pts[1].fY == bounds.fTop ?
                            SkRRect::kUpperRight_Corner : SkRRect::kLowerRight_Corner;
            PkASSERT(!radii[corner].fX && !radii[corner].fY);
            radii[corner] = dxdy;
        } else {
            PkASSERT((verb == SkPath::kLine_Verb
                    && (!(pts[1].fX - pts[0].fX) || !(pts[1].fY - pts[0].fY)))
                    || verb == SkPath::kClose_Verb);
        }
    }
    SkRRect rrect;
    rrect.setRectRadii(bounds, radii);
    return rrect;
}

SkPathRef::Iter::Iter() {
    // need to init enough to make next() harmlessly return kDone_Verb
    fVerbs = nullptr;
    fVerbStop = nullptr;
}

SkPathRef::Iter::Iter(const SkPathRef& path) {
    this->setPathRef(path);
}

void SkPathRef::Iter::setPathRef(const SkPathRef& path) {
    fPts = path.points();
    fVerbs = path.verbsBegin();
    fVerbStop = path.verbsEnd();
    fConicWeights = path.conicWeights();
    if (fConicWeights) {
        fConicWeights -= 1;  // begin one behind
    }

    // Don't allow iteration through non-finite points.
    if (!path.isFinite()) {
        fVerbStop = fVerbs;
    }
}

uint8_t SkPathRef::Iter::next(SkPoint pts[4]) {
    if (fVerbs == fVerbStop) {
        return (uint8_t) SkPath::kDone_Verb;
    }

    // fVerbs points one beyond next verb so decrement first.
    unsigned verb = *fVerbs++;
    const SkPoint* srcPts = fPts;

    switch (verb) {
        case SkPath::kMove_Verb:
            pts[0] = srcPts[0];
            srcPts += 1;
            break;
        case SkPath::kLine_Verb:
            pts[0] = srcPts[-1];
            pts[1] = srcPts[0];
            srcPts += 1;
            break;
        case SkPath::kConic_Verb:
            fConicWeights += 1;
            [[fallthrough]];
        case SkPath::kQuad_Verb:
            pts[0] = srcPts[-1];
            pts[1] = srcPts[0];
            pts[2] = srcPts[1];
            srcPts += 2;
            break;
        case SkPath::kCubic_Verb:
            pts[0] = srcPts[-1];
            pts[1] = srcPts[0];
            pts[2] = srcPts[1];
            pts[3] = srcPts[2];
            srcPts += 3;
            break;
        case SkPath::kClose_Verb:
            break;
        case SkPath::kDone_Verb:
            break;
    }
    fPts = srcPts;
    return (uint8_t) verb;
}

uint8_t SkPathRef::Iter::peek() const {
    return fVerbs < fVerbStop ? *fVerbs : (uint8_t) SkPath::kDone_Verb;
}

SkPathEdgeIter::SkPathEdgeIter(const SkPath& path) {
    fMoveToPtr = fPts = path.fPathRef->points();
    fVerbs = path.fPathRef->verbsBegin();
    fVerbsStop = path.fPathRef->verbsEnd();
    fConicWeights = path.fPathRef->conicWeights();
    if (fConicWeights) {
        fConicWeights -= 1;  // begin one behind
    }

    fNeedsCloseLine = false;
    fNextIsNewContour = false;
}
}  // namespace pk