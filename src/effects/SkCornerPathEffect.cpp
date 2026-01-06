/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/effects/SkCornerPathEffect.h"
#include <include/core/SkPathMeasure.h>
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "src/core/SkPathEffectBase.h"
#include <array>
#include <limits>

namespace pk {
namespace {

constexpr float kCornerEffectTolerance = 1E-4f;
constexpr float kPathMeasureResScale = 10.f;

// Curve segment structure
struct CurveSegment {
    SkPath::Verb verb;
    std::array<SkPoint, 4> points;
    SkScalar conicWeight = 1.0f;  // Conic weight, ignored for other curve types
};

// Draw CurveSegment to SkPath (without moveTo)
void DrawCurveSegment(const CurveSegment& curve, SkPath* dst) {
    switch (curve.verb) {
        case SkPath::kLine_Verb:
            dst->lineTo(curve.points[1]);
            break;
        case SkPath::kQuad_Verb:
            dst->quadTo(curve.points[1], curve.points[2]);
            break;
        case SkPath::kConic_Verb:
            dst->conicTo(curve.points[1], curve.points[2], curve.conicWeight);
            break;
        case SkPath::kCubic_Verb:
            dst->cubicTo(curve.points[1], curve.points[2], curve.points[3]);
            break;
        default:
            break;
    }
}

// Build SkPathMeasure from CurveSegment
SkPathMeasure BuildMeasure(const CurveSegment& curve) {
    SkPath path;
    path.moveTo(curve.points[0]);
    switch (curve.verb) {
        case SkPath::kLine_Verb:
            path.lineTo(curve.points[1]);
            break;
        case SkPath::kQuad_Verb:
            path.quadTo(curve.points[1], curve.points[2]);
            break;
        case SkPath::kConic_Verb:
            path.conicTo(curve.points[1], curve.points[2], curve.conicWeight);
            break;
        case SkPath::kCubic_Verb:
            path.cubicTo(curve.points[1], curve.points[2], curve.points[3]);
            break;
        default:
            break;
    }
    return SkPathMeasure(path, false, kPathMeasureResScale);
}

float ComputeTangentDistances(SkVector v1, SkVector v2, float radius) {
    // Calculate the angle between the two vectors
    auto dotProduct = std::max(-1.0f, std::min(1.0f, v1.dot(v2)));
    auto halfAngle = std::acos(dotProduct) / 2.0f;
    // Guard against division by zero when angle is near 0 or 180 degrees
    auto tanHalfAngle = std::tan(halfAngle);
    if (std::abs(tanHalfAngle) < kCornerEffectTolerance) {
        return std::numeric_limits<float>::max();
    }
    // The distance from the angle center to each tangent point
    return radius / tanHalfAngle;
}

float ArcCubicBezierHandleLength(SkPoint start,
                                 SkVector startTangent,
                                 SkPoint end,
                                 SkVector endTangent) {
    auto chordLength = (end - start).length();
    auto dotProduct = startTangent.dot(endTangent);
    auto cosAngle = std::max(-1.0f, std::min(1.0f, dotProduct));
    auto angle = std::acos(cosAngle);
    // Guard against division by zero when angle is near 0
    auto sinHalfAngle = std::sin(angle / 2.f);
    if (std::abs(sinHalfAngle) < kCornerEffectTolerance) {
        return 0.0f;
    }
    auto handleLength = (4.f * (1 - std::cos(angle / 2.f))) / (3 * sinHalfAngle);
    auto radius = (chordLength / 2.f) / sinHalfAngle;
    return handleLength * radius;
}

bool SkPointsNearlyEqual(const SkPoint& p1, const SkPoint& p2) {
    return SkScalarNearlyEqual(p1.fX, p2.fX, kCornerEffectTolerance) &&
           SkScalarNearlyEqual(p1.fY, p2.fY, kCornerEffectTolerance);
}

}  // namespace

class SkCornerPathEffectImpl : public SkPathEffectBase {
public:
    explicit SkCornerPathEffectImpl(SkScalar radius) : fRadius(radius) {}

    // Process path by contours
    bool onFilterPath(SkPath* dst,
                      const SkPath& src,
                      SkStrokeRec*,
                      const SkRect*,
                      const SkMatrix&) const override {
        if (fRadius <= 0) {
            return false;
        }

        // Extract curve segments directly, separated by contours
        SkPath::Iter iter(src, false);
        SkPath::Verb verb;
        std::array<SkPoint, 4> points;

        std::vector<CurveSegment> currentContourCurves;
        bool hasMove = false;

        while ((verb = iter.next(points.data())) != SkPath::kDone_Verb) {
            if (verb == SkPath::kMove_Verb) {
                // Process previous contour
                if (hasMove && !currentContourCurves.empty()) {
                    ProcessContourCurves(currentContourCurves, false, dst);
                    currentContourCurves.clear();
                }
                hasMove = true;
                continue;
            }

            if (verb == SkPath::kClose_Verb) {
                // Process closed contour
                if (!currentContourCurves.empty()) {
                    ProcessContourCurves(currentContourCurves, true, dst);
                    currentContourCurves.clear();
                }
                hasMove = false;
                continue;
            }

            // Extract and normalize curve segment
            CurveSegment segment;
            segment.points = points;
            segment.verb = verb;

            if (verb == SkPath::kLine_Verb) {
                if (SkPointsNearlyEqual(points[0], points[1])) {
                    continue;
                }
            } else if (verb == SkPath::kQuad_Verb) {
                if (SkPointsNearlyEqual(points[0], points[2])) {
                    continue;
                }
            } else if (verb == SkPath::kConic_Verb) {
                if (SkPointsNearlyEqual(points[0], points[2])) {
                    continue;
                }
                segment.conicWeight = iter.conicWeight();
            } else if (verb == SkPath::kCubic_Verb) {
                if (SkPointsNearlyEqual(points[0], points[3])) {
                    continue;
                }
            }
            currentContourCurves.push_back(segment);
        }

        // Process last contour (open path)
        if (hasMove && !currentContourCurves.empty()) {
            ProcessContourCurves(currentContourCurves, false, dst);
        }

        return true;
    }

    bool computeFastBounds(SkRect*) const override {
        // Rounding sharp corners within a path produces a new path that is still contained within
        // the original's bounds, so leave 'bounds' unmodified.
        return true;
    }

private:
    // Build corner arc curve between two adjacent curves.
    // Note: startCurve and endCurve will be modified (trimmed) in place.
    bool BuildCornerCurve(CurveSegment& startCurve,
                          float startTangentDistanceLimit,
                          CurveSegment& endCurve,
                          float endTangentDistanceLimit,
                          SkScalar radius,
                          CurveSegment& arcCurve) const {
        SkPathMeasure startMeasure = BuildMeasure(startCurve);
        SkPathMeasure endMeasure = BuildMeasure(endCurve);
        auto startCurveLength = startMeasure.getLength();
        auto endCurveLength = endMeasure.getLength();

        SkVector startDir;
        if (!startMeasure.getPosTan(startCurveLength, nullptr, &startDir)) {
            return false;
        }
        startDir = -startDir;
        startDir.normalize();
        SkVector endDir;
        if (!endMeasure.getPosTan(0, nullptr, &endDir)) {
            return false;
        }
        endDir.normalize();
        if (SkScalarNearlyEqual(startDir.fX, -endDir.fX, kCornerEffectTolerance) &&
            SkScalarNearlyEqual(startDir.fY, -endDir.fY, kCornerEffectTolerance)) {
            return false;
        }

        auto tangentDistance = ComputeTangentDistances(startDir, endDir, radius);
        auto startTangentDistance = std::min(tangentDistance, startTangentDistanceLimit);
        auto endTangentDistance = std::min(tangentDistance, endTangentDistanceLimit);
        tangentDistance = std::min(startTangentDistance, endTangentDistance);

        SkPoint startTangentPoint;
        SkVector startTangentVector;
        bool startSuccess = startMeasure.getPosTan(
                startCurveLength - tangentDistance, &startTangentPoint, &startTangentVector);
        SkPoint endTangentPoint;
        SkVector endTangentVector;
        bool endSuccess =
                endMeasure.getPosTan(tangentDistance, &endTangentPoint, &endTangentVector);
        if (!startSuccess || !endSuccess) {
            return false;
        }

        // modify startCurve
        {
            SkPath startSegment;
            startMeasure.getSegment(0.f, startCurveLength - tangentDistance, &startSegment, true);
            SkPath::Iter iter(startSegment, false);
            std::array<SkPoint, 4> points;
            auto verb = iter.next(points.data());
            verb = iter.next(points.data());
            startCurve.points = points;
            startCurve.verb = verb;
            startCurve.conicWeight = verb == SkPath::kConic_Verb ? iter.conicWeight() : 1.0f;
        }

        // modify endCurve
        {
            SkPath endSegment;
            endMeasure.getSegment(tangentDistance, endCurveLength, &endSegment, true);
            SkPath::Iter iter(endSegment, false);
            std::array<SkPoint, 4> points;
            auto verb = iter.next(points.data());
            verb = iter.next(points.data());
            endCurve.points = points;
            endCurve.verb = verb;
            endCurve.conicWeight = verb == SkPath::kConic_Verb ? iter.conicWeight() : 1.0f;
        }

        // build arc curve
        auto handleLength = ArcCubicBezierHandleLength(
                startTangentPoint, startTangentVector, endTangentPoint, endTangentVector);
        arcCurve.verb = SkPath::kCubic_Verb;
        arcCurve.points = {startTangentPoint,
                           startTangentPoint + startTangentVector * handleLength,
                           endTangentPoint - endTangentVector * handleLength,
                           endTangentPoint};
        return true;
    }

    // Process curve segments of a single contour
    void ProcessContourCurves(std::vector<CurveSegment>& curves, bool closed, SkPath* dst) const {
        // Return if no valid curves
        if (curves.empty()) {
            return;
        }

        // Handle single-curve contour (no corner rounding needed)
        if (curves.size() == 1) {
            dst->moveTo(curves[0].points[0]);
            DrawCurveSegment(curves[0], dst);
            if (closed) {
                dst->close();
            }
            return;
        }

        // Apply corner rounding to multi-curve contour
        ProcessMultiCurveContour(curves, closed, dst);
    }

    // Process multi-curve contour
    void ProcessMultiCurveContour(std::vector<CurveSegment>& curves,
                                  bool closed,
                                  SkPath* dst) const {
        const size_t numCurves = curves.size();

        // Calculate length of each curve
        std::vector<float> curveLengths(numCurves);
        for (size_t i = 0; i < numCurves; ++i) {
            curveLengths[i] = BuildMeasure(curves[i]).getLength();
        }

        // For closed path: calculate corner between last and first curve
        CurveSegment firstArcCurve;
        bool hasFirstArc = false;

        if (closed) {
            hasFirstArc = BuildCornerCurve(curves[numCurves - 1],
                                           curveLengths[numCurves - 1] * 0.5f,
                                           curves[0],
                                           curveLengths[0] * 0.5f,
                                           fRadius,
                                           firstArcCurve);
        }

        // Start drawing contour
        if (hasFirstArc) {
            // Draw corner arc between last and first curve
            dst->moveTo(firstArcCurve.points[0]);
            dst->cubicTo(firstArcCurve.points[1], firstArcCurve.points[2], firstArcCurve.points[3]);
        } else {
            dst->moveTo(curves[0].points[0]);
        }

        // Process each pair of adjacent curves
        for (size_t i = 0; i < numCurves - 1; ++i) {
            CurveSegment arcCurve;
            float startLimit = curveLengths[i] * (i == 0 && !closed ? 1.0f : 0.5f);
            float endLimit = curveLengths[i + 1] * (i == numCurves - 2 && !closed ? 1.0f : 0.5f);

            bool insertArc = BuildCornerCurve(
                    curves[i], startLimit, curves[i + 1], endLimit, fRadius, arcCurve);
            // Draw current curve (endpoints may have been modified by BuildCornerCurve)
            DrawCurveSegment(curves[i], dst);
            if (insertArc) {
                dst->cubicTo(arcCurve.points[1], arcCurve.points[2], arcCurve.points[3]);
            }
        }

        DrawCurveSegment(curves.back(), dst);

        // Close path: handle start-end connection
        if (closed) {
            dst->close();
        }
    }

    const SkScalar fRadius;

    using INHERITED = SkPathEffectBase;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

sk_sp<SkPathEffect> SkCornerPathEffect::Make(SkScalar radius) {
    return SkScalarIsFinite(radius) && (radius > 0)
                   ? sk_sp<SkPathEffect>(new SkCornerPathEffectImpl(radius))
                   : nullptr;
}
}  // namespace pk