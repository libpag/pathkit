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

namespace pk {
namespace {
float ComputeTangentDistances(SkVector v1, SkVector v2, float radius) {
    // Calculate the angle between the two vectors
    auto dotProduct = v1.dot(v2);
    auto halfAngle = std::acos(dotProduct) / 2.0f;
    // The distance from the angle center to each tangent point
    return radius / std::tan(halfAngle);
}

float ArcCubicBezierHandleLength(SkPoint start,
                                 SkVector startTangent,
                                 SkPoint end,
                                 SkVector endTangent) {
    auto chordLength = (end - start).length();
    auto dotProduct = startTangent.dot(endTangent);
    auto cosAngle = std::max(-1.0f, std::min(1.0f, dotProduct));
    auto angle = std::acos(cosAngle);
    auto handleLength = (4.f * (1 - std::cos(angle / 2.f))) / (3 * std::sin(angle / 2.f));
    auto radius = (chordLength / 2.f) / std::sin(angle / 2.f);
    return handleLength * radius;
}

bool SkPointsNearlyEqual(const SkPoint& p1, const SkPoint& p2) {
    return SkScalarNearlyEqual(p1.fX, p2.fX, 1E-4f) && SkScalarNearlyEqual(p1.fY, p2.fY, 1E-4f);
}

bool BuildCornerCurve(std::array<SkPoint, 4>& startCurve,
                      const std::shared_ptr<SkPathMeasure>& startMeasure,
                      std::array<SkPoint, 4>& endCurve,
                      const std::shared_ptr<SkPathMeasure>& endMeasure,
                      SkScalar radius,
                      std::array<SkPoint, 4>& arcCurve) {
    auto startDir = startCurve[2] - startCurve[3];
    startDir.normalize();
    auto endDir = endCurve[1] - endCurve[0];
    endDir.normalize();
    if (SkScalarNearlyEqual(startDir.fX, -endDir.fX, 1E-4f) &&
        SkScalarNearlyEqual(startDir.fY, -endDir.fY, 1E-4f)) {
        return false;
    }

    auto tangentDistance = ComputeTangentDistances(startDir, endDir, radius);
    auto startCurveLength = startMeasure->getLength();
    auto startTangentDistance = std::min(tangentDistance, startCurveLength / 2.0f);
    auto endCurveLength = endMeasure->getLength();
    auto endTangentDistance = std::min(tangentDistance, endCurveLength / 2.0f);
    tangentDistance = std::min(startTangentDistance, endTangentDistance);

    SkPoint startTangentPoint;
    SkVector startTangentVector;
    bool startSuccess = startMeasure->getPosTan(
            startCurveLength - tangentDistance, &startTangentPoint, &startTangentVector);
    SkPoint endTangentPoint;
    SkVector endTangentVector;
    bool endSuccess = endMeasure->getPosTan(tangentDistance, &endTangentPoint, &endTangentVector);
    if (!startSuccess || !endSuccess) {
        return false;
    }

    // modify startCurve
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

    // modify endCurve
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

    // build arc curve
    auto handleLength = ArcCubicBezierHandleLength(
            startTangentPoint, startTangentVector, endTangentPoint, endTangentVector);
    arcCurve = {startTangentPoint,
                startTangentPoint + startTangentVector * handleLength,
                endTangentPoint - endTangentVector * handleLength,
                endTangentPoint};
    return true;
}
}  // namespace

class SkCornerPathEffectImpl : public SkPathEffectBase {
public:
    explicit SkCornerPathEffectImpl(SkScalar radius) : fRadius(radius) {}

    bool onFilterPathOld(
            SkPath* dst, const SkPath& src, SkStrokeRec*, const SkRect*, const SkMatrix&) const {
        if (fRadius <= 0) {
            return false;
        }

        SkPath::Iter iter(src, false);
        SkPath::Verb verb = SkPath::kDone_Verb;
        std::array<SkPoint, 4> points;

        bool closed = false;
        bool addPrevCurve = true;

        std::array<SkPoint, 4> curPoints;
        std::shared_ptr<SkPathMeasure> curMeasure = nullptr;
        SkPath::Verb prevVerb = SkPath::kDone_Verb;
        std::array<SkPoint, 4> prevPoints;
        std::shared_ptr<SkPathMeasure> prevMeasure = nullptr;

        SkPath::Verb contourBeginCurveVerb = SkPath::kDone_Verb;
        std::array<SkPoint, 4> contourBeginCurvePoints;
        std::shared_ptr<SkPathMeasure> contourBeginCurveMeasure = nullptr;

        while (true) {
            bool isInvalidCurve = false;
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
                    isInvalidCurve = SkPointsNearlyEqual(curPoints[0], curPoints[3]);
                    break;
                case SkPath::kQuad_Verb:
                    curPoints[0] = prevPoints[3];
                    curPoints[1] = curPoints[0] + (points[1] - curPoints[0]) * (2.f / 3.f);
                    curPoints[2] = points[2] + (points[1] - points[2]) * (2.f / 3.f);
                    curPoints[3] = points[2];
                    verb = SkPath::kCubic_Verb;
                    isInvalidCurve = SkPointsNearlyEqual(curPoints[0], curPoints[3]);
                    break;
                case SkPath::kConic_Verb: {
                    curPoints[0] = prevPoints[3];
                    float factor = (2.0f / 3.0f) * (iter.conicWeight() /
                                                    (1.0f + (iter.conicWeight() - 1.0f) / 3.0f));
                    curPoints[1] = curPoints[0] + (points[1] - curPoints[0]) * factor;
                    curPoints[2] = points[2] + (points[1] - points[2]) * factor;
                    curPoints[3] = points[2];
                    verb = SkPath::kCubic_Verb;
                    isInvalidCurve = SkPointsNearlyEqual(curPoints[0], curPoints[3]);
                    break;
                }
                case SkPath::kCubic_Verb:
                    curPoints[0] = prevPoints[3];
                    curPoints[1] = points[1];
                    curPoints[2] = points[2];
                    curPoints[3] = points[3];
                    isInvalidCurve = SkPointsNearlyEqual(curPoints[0], curPoints[3]);
                    break;
                case SkPath::kClose_Verb:
                case SkPath::kDone_Verb:
                    break;
                default:
                    PkDEBUGFAIL("default should not be reached");
                    return false;
            }
            // invalid curves are ignored
            if (isInvalidCurve) {
                continue;
            }
            if (verb == SkPath::kMove_Verb) {
                prevVerb = verb;
                prevPoints = curPoints;
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
                dst->moveTo(prevPoints[3]);
                if (closed) {
                    contourBeginCurveVerb = verb;
                    contourBeginCurvePoints = curPoints;
                }
                prevVerb = verb;
                prevPoints = curPoints;
                addPrevCurve = !closed;
                prevMeasure = curMeasure;
                continue;
            }

            if (verb != SkPath::kClose_Verb && verb != SkPath::kDone_Verb) {
                std::array<SkPoint, 4> arcPoints;
                bool insertArc = BuildCornerCurve(
                        prevPoints, prevMeasure, curPoints, curMeasure, fRadius, arcPoints);
                if (addPrevCurve) {
                    if (prevVerb == SkPath::kLine_Verb) {
                        dst->lineTo(prevPoints[3]);
                    } else if (prevVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(prevPoints[1], prevPoints[2], prevPoints[3]);
                    }
                } else {
                    contourBeginCurvePoints[2] = prevPoints[2];
                    contourBeginCurvePoints[3] = prevPoints[3];
                    contourBeginCurveMeasure = prevMeasure;
                    dst->moveTo(prevPoints[3]);
                }
                if (insertArc) {
                    dst->cubicTo(arcPoints[1], arcPoints[2], arcPoints[3]);
                }
                prevVerb = verb;
                prevPoints = curPoints;
                prevMeasure = curMeasure;
            } else {
                if (!prevMeasure) {
                    return true;
                }
                if (closed) {
                    std::array<SkPoint, 4> arcPoints;
                    auto insertArc = BuildCornerCurve(prevPoints,
                                                      prevMeasure,
                                                      contourBeginCurvePoints,
                                                      contourBeginCurveMeasure,
                                                      fRadius,
                                                      arcPoints);
                    if (prevVerb == SkPath::kLine_Verb) {
                        dst->lineTo(prevPoints[3]);
                    } else if (prevVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(prevPoints[1], prevPoints[2], prevPoints[3]);
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
                        dst->lineTo(prevPoints[3]);
                    } else if (prevVerb == SkPath::kCubic_Verb) {
                        dst->cubicTo(prevPoints[1], prevPoints[2], prevPoints[3]);
                    }
                }
                prevVerb = verb;
                prevPoints = curPoints;
                prevMeasure = nullptr;
                if (verb == SkPath::kDone_Verb) {
                    return true;
                }
            }
            addPrevCurve = true;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////////

    // 新实现：按轮廓分离处理
    bool onFilterPath(SkPath* dst,
                      const SkPath& src,
                      SkStrokeRec*,
                      const SkRect*,
                      const SkMatrix&) const override {
        if (fRadius <= 0) {
            return false;
        }

        // 直接提取曲线段，按轮廓分离
        SkPath::Iter iter(src, false);
        SkPath::Verb verb;
        std::array<SkPoint, 4> points;
        std::array<SkPoint, 4> prevPoints;

        std::vector<CurveSegment> currentContourCurves;
        bool hasMove = false;

        while ((verb = iter.next(points.data())) != SkPath::kDone_Verb) {
            if (verb == SkPath::kMove_Verb) {
                // 处理前一个轮廓
                if (hasMove && !currentContourCurves.empty()) {
                    ProcessContourCurves(currentContourCurves, false, dst);
                    currentContourCurves.clear();
                }
                // 记录新轮廓的起点
                prevPoints[3] = points[0];
                hasMove = true;
                continue;
            }

            if (verb == SkPath::kClose_Verb) {
                // 处理闭合轮廓
                if (!currentContourCurves.empty()) {
                    ProcessContourCurves(currentContourCurves, true, dst);
                    currentContourCurves.clear();
                }
                hasMove = false;
                continue;
            }

            // 提取并归一化曲线段
            CurveSegment segment;
            segment.verb = verb;
            bool isInvalidCurve = false;

            switch (verb) {
                case SkPath::kLine_Verb:
                    segment.points[0] = points[0];
                    segment.points[1] = points[1];
                    segment.points[2] = points[0];
                    segment.points[3] = points[1];
                    isInvalidCurve = SkPointsNearlyEqual(segment.points[0], segment.points[3]);
                    break;

                case SkPath::kQuad_Verb:
                    // 二次贝塞尔曲线升阶为三次
                    segment.points[0] = prevPoints[3];
                    segment.points[1] =
                            segment.points[0] + (points[1] - segment.points[0]) * (2.f / 3.f);
                    segment.points[2] = points[2] + (points[1] - points[2]) * (2.f / 3.f);
                    segment.points[3] = points[2];
                    segment.verb = SkPath::kCubic_Verb;
                    isInvalidCurve = SkPointsNearlyEqual(segment.points[0], segment.points[3]);
                    break;

                case SkPath::kConic_Verb: {
                    // 圆锥曲线转换为三次贝塞尔曲线
                    segment.points[0] = prevPoints[3];
                    float factor = (2.0f / 3.0f) * (iter.conicWeight() /
                                                    (1.0f + (iter.conicWeight() - 1.0f) / 3.0f));
                    segment.points[1] =
                            segment.points[0] + (points[1] - segment.points[0]) * factor;
                    segment.points[2] = points[2] + (points[1] - points[2]) * factor;
                    segment.points[3] = points[2];
                    segment.verb = SkPath::kCubic_Verb;
                    isInvalidCurve = SkPointsNearlyEqual(segment.points[0], segment.points[3]);
                    break;
                }

                case SkPath::kCubic_Verb:
                    segment.points[0] = prevPoints[3];
                    segment.points[1] = points[1];
                    segment.points[2] = points[2];
                    segment.points[3] = points[3];
                    isInvalidCurve = SkPointsNearlyEqual(segment.points[0], segment.points[3]);
                    break;

                default:
                    continue;
            }

            // 跳过退化曲线
            if (isInvalidCurve) {
                continue;
            }

            // 创建 SkPathMeasure 用于测量曲线长度
            SkPath tempPath;
            if (segment.verb == SkPath::kLine_Verb) {
                tempPath.moveTo(segment.points[0]);
                tempPath.lineTo(segment.points[3]);
            } else if (segment.verb == SkPath::kCubic_Verb) {
                tempPath.moveTo(segment.points[0]);
                tempPath.cubicTo(segment.points[1], segment.points[2], segment.points[3]);
            }
            segment.measure = std::make_shared<SkPathMeasure>(tempPath, false, 10.f);

            currentContourCurves.push_back(segment);
            prevPoints = segment.points;
        }

        // 处理最后一个轮廓（开放路径）
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
    // 曲线段结构体
    struct CurveSegment {
        SkPath::Verb verb;
        std::array<SkPoint, 4> points;
        std::shared_ptr<SkPathMeasure> measure;
    };

    // 处理单个轮廓的曲线段集合
    void ProcessContourCurves(std::vector<CurveSegment>& curves, bool closed, SkPath* dst) const {
        // 如果没有有效曲线，直接返回
        if (curves.empty()) {
            return;
        }

        // 处理单曲线轮廓（无需圆角）
        if (curves.size() == 1) {
            dst->moveTo(curves[0].points[0]);
            if (curves[0].verb == SkPath::kLine_Verb) {
                dst->lineTo(curves[0].points[3]);
            } else {
                dst->cubicTo(curves[0].points[1], curves[0].points[2], curves[0].points[3]);
            }
            if (closed) {
                dst->close();
            }
            return;
        }

        // 对多曲线轮廓进行圆角处理
        ProcessMultiCurveContour(curves, closed, dst);
    }

    // 处理多曲线轮廓
    void ProcessMultiCurveContour(std::vector<CurveSegment>& curves,
                                  bool closed,
                                  SkPath* dst) const {
        const size_t numCurves = curves.size();

        // 为闭合路径准备：计算最后一条和第一条曲线之间的圆角
        std::array<SkPoint, 4> firstArcPoints;
        bool hasFirstArc = false;

        if (closed) {
            hasFirstArc = BuildCornerCurve(curves[numCurves - 1].points,
                                           curves[numCurves - 1].measure,
                                           curves[0].points,
                                           curves[0].measure,
                                           fRadius,
                                           firstArcPoints);
        }

        // 开始绘制轮廓
        dst->moveTo(curves[0].points[0]);

        // 处理每对相邻曲线
        for (size_t i = 0; i < numCurves; ++i) {
            auto& curCurve = curves[i];

            // 绘制当前曲线（可能已被 BuildCornerCurve 修改过端点）
            if (curCurve.verb == SkPath::kLine_Verb) {
                dst->lineTo(curCurve.points[3]);
            } else if (curCurve.verb == SkPath::kCubic_Verb) {
                dst->cubicTo(curCurve.points[1], curCurve.points[2], curCurve.points[3]);
            }

            // 计算并插入下一个圆角（除了最后一条曲线）
            if (i < numCurves - 1) {
                std::array<SkPoint, 4> arcPoints;
                bool insertArc = BuildCornerCurve(curCurve.points,
                                                  curCurve.measure,
                                                  curves[i + 1].points,
                                                  curves[i + 1].measure,
                                                  fRadius,
                                                  arcPoints);

                if (insertArc) {
                    dst->cubicTo(arcPoints[1], arcPoints[2], arcPoints[3]);
                }
            }
        }

        // 闭合路径：处理首尾连接
        if (closed) {
            if (hasFirstArc) {
                dst->cubicTo(firstArcPoints[1], firstArcPoints[2], firstArcPoints[3]);
            }
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