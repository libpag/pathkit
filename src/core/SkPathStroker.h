/*
 * Copyright 2025 The PathKit Authors
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPathStroker_DEFINED
#define SkPathStroker_DEFINED

#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkScalar.h"
#include "include/core/SkStrokeParams.h"
#include "src/core/SkGeometry.h"

namespace pk {

struct SkQuadConstruct;

// Helper class to manage temporary path swap
// If dst points to src, we need to swap its contents with src when we're done.
class AutoTmpPath {
public:
    AutoTmpPath(const SkPath& src, SkPath** dst) : fSrc(src) {
        if (&src == *dst) {
            *dst = &fTmpDst;
            fSwapWithSrc = true;
        } else {
            (*dst)->reset();
            fSwapWithSrc = false;
        }
    }

    ~AutoTmpPath() {
        if (fSwapWithSrc) {
            fTmpDst.swap(*const_cast<SkPath*>(&fSrc));
        }
    }

private:
    SkPath          fTmpDst;
    const SkPath&   fSrc;
    bool            fSwapWithSrc;
};

class SkPathStroker {
public:
    SkPathStroker(const SkPath& src, SkScalar radius, SkScalar resScale, bool canIgnoreCenter);

    bool hasOnlyMoveTo() const { return 0 == fSegmentCount; }
    SkPoint moveToPt() const { return fFirstPt; }

    void moveTo(const SkPoint&);
    void lineTo(const SkPoint&, const SkStrokeParams& params, const SkPath::Iter* iter = nullptr);
    void quadTo(const SkPoint&, const SkPoint&, const SkStrokeParams& params);
    void conicTo(const SkPoint&, const SkPoint&, SkScalar weight, const SkStrokeParams& params);
    void cubicTo(const SkPoint&, const SkPoint&, const SkPoint&, const SkStrokeParams& params);
    void close(bool isLine, const SkStrokeParams& params) {
        this->finishContour(true, isLine, params);
    }

    void done(SkPath* dst, bool isLine, const SkStrokeParams& params) {
        this->finishContour(false, isLine, params);
        dst->swap(fOuter);
    }

    SkScalar getResScale() const { return fResScale; }

    bool isCurrentContourEmpty() const {
        return fInner.isZeroLengthSincePoint(0) &&
               fOuter.isZeroLengthSincePoint(fFirstOuterPtIndexInContour);
    }

private:
    SkScalar fRadius;
    SkScalar fResScale;
    SkScalar fInvResScale;
    SkScalar fInvResScaleSquared;

    SkVector fFirstNormal, fPrevNormal, fFirstUnitNormal, fPrevUnitNormal;
    SkPoint fFirstPt, fPrevPt;  // on original path
    SkPoint fFirstOuterPt;
    int fFirstOuterPtIndexInContour;
    int fSegmentCount;
    bool fPrevIsLine;
    bool fCanIgnoreCenter;

    // Store the first stroke params for closing contours
    SkStrokeParams fFirstParams;
    // Store the previous stroke params
    SkStrokeParams fPrevParams;

    SkPath fInner, fOuter, fCusper;  // outer is our working answer, inner is temp

    enum StrokeType {
        kOuter_StrokeType = 1,  // use sign-opposite values later to flip perpendicular axis
        kInner_StrokeType = -1
    } fStrokeType;

    enum ResultType {
        kSplit_ResultType,       // the caller should split the quad stroke in two
        kDegenerate_ResultType,  // the caller should add a line
        kQuad_ResultType,        // the caller should (continue to try to) add a quad stroke
    };

    enum ReductionType {
        kPoint_ReductionType,        // all curve points are practically identical
        kLine_ReductionType,         // the control point is on the line between the ends
        kQuad_ReductionType,         // the control point is outside the line between the ends
        kDegenerate_ReductionType,   // the control point is on the line but outside the ends
        kDegenerate2_ReductionType,  // two control points are on the line but outside ends (cubic)
        kDegenerate3_ReductionType,  // three areas of max curvature found (for cubic)
    };

    enum IntersectRayType {
        kCtrlPt_RayType,
        kResultType_RayType,
    };

    int fRecursionDepth;  // track stack depth to abort if numerics run amok
    bool fFoundTangents;  // do less work until tangents meet (cubic)
    bool fJoinCompleted;  // previous join was not degenerate

    void addDegenerateLine(const SkQuadConstruct*);
    static ReductionType CheckConicLinear(const SkConic&, SkPoint* reduction);
    static ReductionType CheckCubicLinear(const SkPoint cubic[4],
                                          SkPoint reduction[3],
                                          const SkPoint** tanPtPtr);
    static ReductionType CheckQuadLinear(const SkPoint quad[3], SkPoint* reduction);
    ResultType compareQuadConic(const SkConic&, SkQuadConstruct*) const;
    ResultType compareQuadCubic(const SkPoint cubic[4], SkQuadConstruct*);
    ResultType compareQuadQuad(const SkPoint quad[3], SkQuadConstruct*);
    void conicPerpRay(
            const SkConic&, SkScalar t, SkPoint* tPt, SkPoint* onPt, SkPoint* tangent) const;
    void conicQuadEnds(const SkConic&, SkQuadConstruct*) const;
    bool conicStroke(const SkConic&, SkQuadConstruct*);
    bool cubicMidOnLine(const SkPoint cubic[4], const SkQuadConstruct*) const;
    void cubicPerpRay(const SkPoint cubic[4],
                      SkScalar t,
                      SkPoint* tPt,
                      SkPoint* onPt,
                      SkPoint* tangent) const;
    void cubicQuadEnds(const SkPoint cubic[4], SkQuadConstruct*);
    void cubicQuadMid(const SkPoint cubic[4], const SkQuadConstruct*, SkPoint* mid) const;
    bool cubicStroke(const SkPoint cubic[4], SkQuadConstruct*);
    void init(StrokeType strokeType, SkQuadConstruct*, SkScalar tStart, SkScalar tEnd);
    ResultType intersectRay(SkQuadConstruct*, IntersectRayType, int depth = 0) const;
    bool ptInQuadBounds(const SkPoint quad[3], const SkPoint& pt) const;
    void quadPerpRay(
            const SkPoint quad[3], SkScalar t, SkPoint* tPt, SkPoint* onPt, SkPoint* tangent) const;
    bool quadStroke(const SkPoint quad[3], SkQuadConstruct*);
    void setConicEndNormal(const SkConic&,
                           const SkVector& normalAB,
                           const SkVector& unitNormalAB,
                           SkVector* normalBC,
                           SkVector* unitNormalBC);
    void setCubicEndNormal(const SkPoint cubic[4],
                           const SkVector& normalAB,
                           const SkVector& unitNormalAB,
                           SkVector* normalCD,
                           SkVector* unitNormalCD);
    void setQuadEndNormal(const SkPoint quad[3],
                          const SkVector& normalAB,
                          const SkVector& unitNormalAB,
                          SkVector* normalBC,
                          SkVector* unitNormalBC);
    void setRayPts(const SkPoint& tPt, SkVector* dxy, SkPoint* onPt, SkPoint* tangent) const;
    ResultType strokeCloseEnough(const SkPoint stroke[3],
                                 const SkPoint ray[2],
                                 SkQuadConstruct*,
                                 int depth = 0) const;
    ResultType tangentsMeet(const SkPoint cubic[4], SkQuadConstruct*);

    void finishContour(bool close, bool isLine, const SkStrokeParams& params);
    bool preJoinTo(const SkPoint&,
                   SkVector* normal,
                   SkVector* unitNormal,
                   bool isLine,
                   const SkStrokeParams& params);
    void postJoinTo(const SkPoint&,
                    const SkVector& normal,
                    const SkVector& unitNormal,
                    const SkStrokeParams& params);

    void line_to(const SkPoint& currPt, const SkVector& normal);
};

}  // namespace pk

#endif  // SkPathStroker_DEFINED
