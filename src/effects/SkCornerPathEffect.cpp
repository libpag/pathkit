/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/effects/SkCornerPathEffect.h"
#include <include/core/SkPathMeasure.h>
#include <include/core/SkScalar.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <memory>
#include <utility>
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "src/core/SkPathEffectBase.h"

namespace pk {
static bool ComputeStep(const SkPoint& a, const SkPoint& b, SkScalar radius,
                        SkPoint* step) {
    SkScalar dist = SkPoint::Distance(a, b);

    *step = b - a;
    if (dist <= radius * 2) {
        *step *= PK_ScalarHalf;
        return false;
    } else {
        *step *= radius / dist;
        return true;
    }
}

float calculateTangentDistances(SkVector v1, SkVector v2, float radius) {
    // Calculate the angle between the two vectors
    float dotProduct = v1.dot(v2);
    float halfAngle = std::acos(dotProduct) / 2.0f;
    // The distance from the angle center to each tangent point
    return radius / std::tan(halfAngle);
}

float arcCubicBezierHandleLength(SkPoint start,
                                 SkVector startTangent,
                                 SkPoint end,
                                 SkVector endTangent) {
    float chordLength = (end - start).length();

    float dotProduct = startTangent.dot(endTangent);
    float cosAngle = std::max(-1.0f, std::min(1.0f, dotProduct));
    float angle = std::acos(cosAngle);
    float handleLength = (4.f * (1 - std::cos(angle / 2.f))) / (3 * std::sin(angle / 2.f));

    float radius = (chordLength / 2.f) / std::sin(angle / 2.f);
    return handleLength * radius;
}

bool calculateCornerCurve(std::array<SkPoint, 4>& startCurve,
                          const std::shared_ptr<SkPathMeasure>& startMeasure,
                          std::array<SkPoint, 4>& endCurve,
                          const std::shared_ptr<SkPathMeasure>& endMeasure,
                          SkScalar radius,
                          std::array<SkPoint, 4>& arcCurve) {
    SkVector startDir = startCurve[2] - startCurve[3];
    startDir.normalize();
    SkVector endDir = endCurve[1] - endCurve[0];
    endDir.normalize();
    if (SkScalarNearlyEqual(startDir.fX, -endDir.fX, 1E-4f) &&
        SkScalarNearlyEqual(startDir.fY, -endDir.fY, 1E-4f)) {
        return false;
    }

    auto tangentDistance = calculateTangentDistances(startDir, endDir, radius);

    auto startCurveLength = startMeasure->getLength();
    auto startTangentDistance = std::min(tangentDistance, startCurveLength / 2.0f);
    auto endCurveLength = endMeasure->getLength();
    auto endTangentDistance = std::min(tangentDistance, endCurveLength / 2.0f);
    tangentDistance = std::min(startTangentDistance, endTangentDistance);

    SkPoint startTangentPoint;
    SkVector startTangentVector;
    bool success = startMeasure->getPosTan(
            startCurveLength - tangentDistance, &startTangentPoint, &startTangentVector);
    assert(success);
    {
        SkPath startSegment;
        startMeasure->getSegment(0.f, startCurveLength - tangentDistance, &startSegment, false);
        SkPath::Iter iter(startSegment, false);
        std::array<SkPoint, 4> points;
        auto verb = iter.next(points.data());
        verb = iter.next(points.data());
        if (verb == SkPath::kLine_Verb) {
            startCurve[2] = points[0];
            startCurve[3] = points[1];
        } else if (verb == SkPath::kCubic_Verb) {
            startCurve[2] = points[2];
            startCurve[3] = points[3];
        }
    }

    SkPoint endTangentPoint;
    SkVector endTangentVector;
    success = endMeasure->getPosTan(tangentDistance, &endTangentPoint, &endTangentVector);
    assert(success);
    {
        SkPath endSegment;
        endMeasure->getSegment(tangentDistance, endCurveLength, &endSegment, false);
        SkPath::Iter iter(endSegment, false);
        std::array<SkPoint, 4> points;
        auto verb = iter.next(points.data());
        verb = iter.next(points.data());
        if (verb == SkPath::kLine_Verb) {
            endCurve[0] = points[0];
            endCurve[1] = points[1];
        } else if (verb == SkPath::kCubic_Verb) {
            endCurve[0] = points[0];
            endCurve[1] = points[1];
        }
    }

    auto handleLength = arcCubicBezierHandleLength(
            startTangentPoint, startTangentVector, endTangentPoint, endTangentVector);
    arcCurve = {startTangentPoint,
                startTangentPoint + startTangentVector * handleLength,
                endTangentPoint - endTangentVector * handleLength,
                endTangentPoint};
    return true;
}

class SkCornerPathEffectImpl : public SkPathEffectBase {
public:
    explicit SkCornerPathEffectImpl(SkScalar radius) : fRadius(radius) {
    }

    bool onFilterPath(SkPath* dst,
                      const SkPath& src,
                      SkStrokeRec*,
                      const SkRect*,
                      const SkMatrix&) const override {
        if (fRadius <= 0) {
            return false;
        }

        SkPath::Iter iter(src, false);
        SkPath::Verb verb;
        std::array<SkPoint, 4> points;

        bool closed = false;
        bool preIsAdd = true;

        std::array<SkPoint, 4> curPoints;
        SkPath::Verb prevVerb = SkPath::kDone_Verb;
        std::array<SkPoint, 4> prePoints;
        std::shared_ptr<SkPathMeasure> preMeasure = nullptr;
        std::shared_ptr<SkPathMeasure> curMeasure = nullptr;

        SkPath::Verb contourBeginCurveVerb = SkPath::kDone_Verb;
        std::array<SkPoint, 4> contourBeginCurvePoints;
        std::shared_ptr<SkPathMeasure> contourBeginCurveMeasure = nullptr;

        while (true) {
            switch (verb = iter.next(points.data())) {
                case SkPath::kMove_Verb:
                    curPoints[3] = points[0];
                    closed = iter.isClosedContour();
                    break;
                case SkPath::kLine_Verb:
                    curPoints[0] = points[0];
                    curPoints[1] = points[1];
                    curPoints[2] = points[0];
                    curPoints[3] = points[1];
                    break;
                case SkPath::kQuad_Verb:
                    curPoints[0] = prePoints[3];
                    curPoints[1] = curPoints[0] + (points[1] - curPoints[0]) * (2.f / 3.f);
                    curPoints[2] = points[2] + (points[1] - points[2]) * (2.f / 3.f);
                    curPoints[3] = points[2];
                    verb = SkPath::kCubic_Verb;
                    break;
                case SkPath::kConic_Verb:
                    curPoints[0] = prePoints[3];
                    curPoints[1] = curPoints[0] +
                                   (points[1] - curPoints[0]) * (2.f / 3.f) * iter.conicWeight();
                    curPoints[2] =
                            points[2] + (points[1] - points[2]) * (2.f / 3.f) * iter.conicWeight();
                    curPoints[3] = points[2];
                    verb = SkPath::kCubic_Verb;
                    break;
                case SkPath::kCubic_Verb:
                    curPoints[0] = prePoints[3];
                    curPoints[1] = points[1];
                    curPoints[2] = points[2];
                    curPoints[3] = points[3];
                    break;
                case SkPath::kClose_Verb:
                case SkPath::kDone_Verb:
                    break;
                default:
                    PkDEBUGFAIL("default should not be reached");
                    return false;
            }
            if (verb == SkPath::kMove_Verb) {
                prevVerb = verb;
                prePoints = curPoints;
                continue;
            }
            SkPath tempPath;
            if (verb == SkPath::kLine_Verb) {
                tempPath.moveTo(curPoints[0]);
                tempPath.lineTo(curPoints[1]);
            } else if (verb == SkPath::kCubic_Verb) {
                tempPath.moveTo(curPoints[0]);
                tempPath.cubicTo(curPoints[1], curPoints[2], curPoints[3]);
            }
            curMeasure = std::make_shared<SkPathMeasure>(tempPath, false, 200);
            if (prevVerb == SkPath::kMove_Verb) {
                dst->moveTo(prePoints[3]);
                if (closed) {
                    contourBeginCurveVerb = verb;
                    contourBeginCurvePoints = curPoints;
                }
                prevVerb = verb;
                prePoints = curPoints;
                preIsAdd = !closed;
                preMeasure = curMeasure;
                continue;
            }

            if (verb != SkPath::kClose_Verb && verb != SkPath::kDone_Verb) {
                std::array<SkPoint, 4> arcPoints;
                bool insertArc = calculateCornerCurve(
                        prePoints, preMeasure, curPoints, curMeasure, fRadius, arcPoints);
                if (preIsAdd) {
                    if (prevVerb == SkPath::kLine_Verb) {
                        dst->lineTo(prePoints[3]);
                    } else if (prevVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(prePoints[1], prePoints[2], prePoints[3]);
                    }
                } else {
                    contourBeginCurvePoints[2] = prePoints[2];
                    contourBeginCurvePoints[3] = prePoints[3];
                    contourBeginCurveMeasure = preMeasure;
                    dst->moveTo(prePoints[3]);
                }
                if (insertArc) {
                    dst->cubicTo(arcPoints[1], arcPoints[2], arcPoints[3]);
                }
                prevVerb = verb;
                prePoints = curPoints;
                preMeasure = curMeasure;
            } else {
                if (!preMeasure) {
                    return true;
                }
                if (closed) {
                    std::array<SkPoint, 4> arcPoints;
                    auto insertArc = calculateCornerCurve(prePoints,
                                                          preMeasure,
                                                          contourBeginCurvePoints,
                                                          contourBeginCurveMeasure,
                                                          fRadius,
                                                          arcPoints);
                    if (prevVerb == SkPath::kLine_Verb) {
                        dst->lineTo(prePoints[3]);
                    } else if (prevVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(prePoints[1], prePoints[2], prePoints[3]);
                    }
                    if (insertArc) {
                        dst->cubicTo(arcPoints[1], arcPoints[2], arcPoints[3]);
                    }
                    if (contourBeginCurveVerb == SkPath::kLine_Verb) {
                        dst->lineTo(contourBeginCurvePoints[3]);
                    } else if (contourBeginCurveVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(contourBeginCurvePoints[1],
                                     contourBeginCurvePoints[2],
                                     contourBeginCurvePoints[3]);
                    }
                    dst->close();
                } else {
                    if (prevVerb == SkPath::kLine_Verb) {
                        dst->lineTo(prePoints[3]);
                    } else if (prevVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(prePoints[1], prePoints[2], prePoints[3]);
                    }
                }
                prevVerb = verb;
                prePoints = curPoints;
                preMeasure = nullptr;
                if (verb == SkPath::kDone_Verb) {
                    return true;
                }
            }
            preIsAdd = true;
        }
    }

    // bool onFilterPath(SkPath* dst,
    //                   const SkPath& src,
    //                   SkStrokeRec*,
    //                   const SkRect*,
    //                   const SkMatrix&) const override {
    //     if (fRadius <= 0) {
    //         return false;
    //     }

    //     SkPath::Iter iter(src, false);
    //     SkPath::Verb verb, prevVerb = SkPath::kDone_Verb;
    //     SkPoint pts[4];

    //     bool closed;
    //     SkPoint moveTo, lastCorner;
    //     SkVector firstStep, step, tempStep;
    //     bool prevIsValid = true;

    //     // to avoid warnings
    //     step.set(0, 0);
    //     moveTo.set(0, 0);
    //     firstStep.set(0, 0);
    //     tempStep.set(0, 0);
    //     lastCorner.set(0, 0);
    //     SkPath::Verb firstDrawVerb = SkPath::kDone_Verb;
    //     SkPath::Verb lastDrawVerb = SkPath::kDone_Verb;

    //     for (;;) {
    //         switch (verb = iter.next(pts)) {
    //             case SkPath::kMove_Verb:
    //                 // close out the previous (open) contour
    //                 if (SkPath::kLine_Verb == prevVerb) {
    //                     dst->lineTo(lastCorner);
    //                 }
    //                 closed = iter.isClosedContour();
    //                 if (closed) {
    //                     moveTo = pts[0];
    //                     prevIsValid = false;
    //                 } else {
    //                     dst->moveTo(pts[0]);
    //                     prevIsValid = true;
    //                 }
    //                 break;
    //             case SkPath::kLine_Verb: {
    //                 bool drawSegment = ComputeStep(pts[0], pts[1], fRadius, &step);
    //                 // prev corner
    //                 if (!prevIsValid) {
    //                     dst->moveTo(moveTo + step);
    //                     prevIsValid = true;
    //                 } else {
    //                     dst->quadTo(pts[0].fX, pts[0].fY, pts[0].fX + step.fX, pts[0].fY +
    //                     step.fY);
    //                 }
    //                 if (drawSegment) {
    //                     dst->lineTo(pts[1].fX - step.fX, pts[1].fY - step.fY);
    //                 }
    //                 lastCorner = pts[1];
    //                 prevIsValid = true;
    //                 break;
    //             }
    //             case SkPath::kQuad_Verb:
    //                 // TBD - just replicate the curve for now
    //                 if (!prevIsValid) {
    //                     dst->moveTo(pts[0]);
    //                     prevIsValid = true;
    //                 } else {
    //                     dst->lineTo(pts[0]);
    //                 }
    //                 dst->quadTo(pts[1], pts[2]);
    //                 lastCorner = pts[2];
    //                 firstStep.set(0, 0);
    //                 break;
    //             case SkPath::kConic_Verb:
    //                 // TBD - just replicate the curve for now
    //                 if (!prevIsValid) {
    //                     dst->moveTo(pts[0]);
    //                     prevIsValid = true;
    //                 } else {
    //                     dst->lineTo(pts[0]);
    //                 }
    //                 dst->conicTo(pts[1], pts[2], iter.conicWeight());
    //                 lastCorner = pts[2];
    //                 firstStep.set(0, 0);
    //                 break;
    //             case SkPath::kCubic_Verb:
    //                 if (!prevIsValid) {
    //                     dst->moveTo(pts[0]);
    //                     prevIsValid = true;
    //                 } else {
    //                     dst->lineTo(pts[0]);
    //                 }
    //                 // TBD - just replicate the curve for now
    //                 dst->cubicTo(pts[1], pts[2], pts[3]);
    //                 lastCorner = pts[3];
    //                 firstStep.set(0, 0);
    //                 break;
    //             case SkPath::kClose_Verb:
    //                 lastDrawVerb = prevVerb;
    //                 if (firstDrawVerb == SkPath::kLine_Verb && lastDrawVerb ==
    //                 SkPath::kLine_Verb) {
    //                     firstStep = tempStep;
    //                 }
    //                 if (firstStep.fX || firstStep.fY) {
    //                     dst->quadTo(lastCorner.fX,
    //                                 lastCorner.fY,
    //                                 lastCorner.fX + firstStep.fX,
    //                                 lastCorner.fY + firstStep.fY);
    //                 }
    //                 dst->close();
    //                 prevIsValid = false;
    //                 break;
    //             case SkPath::kDone_Verb:
    //                 if (prevIsValid) {
    //                     dst->lineTo(lastCorner);
    //                 }
    //                 return true;
    //             default:
    //                 PkDEBUGFAIL("default should not be reached");
    //                 return false;
    //         }

    //         if (SkPath::kMove_Verb == prevVerb) {
    //             firstStep = step;
    //             firstDrawVerb = verb;
    //             tempStep = firstStep;
    //         }
    //         prevVerb = verb;
    //     }
    //     return true;
    // }

    bool computeFastBounds(SkRect*) const override {
        // Rounding sharp corners within a path produces a new path that is still contained
        // within the original's bounds, so leave 'bounds' unmodified.
        return true;
    }

private:
    const SkScalar fRadius;

    using INHERITED = SkPathEffectBase;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

sk_sp<SkPathEffect> SkCornerPathEffect::Make(SkScalar radius) {
    return SkScalarIsFinite(radius) && (radius > 0) ?
            sk_sp<SkPathEffect>(new SkCornerPathEffectImpl(radius)) : nullptr;
}
}  // namespace pk