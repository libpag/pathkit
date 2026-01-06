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
// 曲线段结构体
struct CurveSegment {
    SkPath::Verb verb;
    std::array<SkPoint, 4> points;
    SkScalar conicWeight = 1.0f;  // 圆锥曲线权重，其他曲线类型忽略
};

// 根据 CurveSegment 构建 SkPathMeasure
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
    return SkPathMeasure(path, false);
}

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

}  // namespace

class SkCornerPathEffectImpl : public SkPathEffectBase {
public:
    explicit SkCornerPathEffectImpl(SkScalar radius) : fRadius(radius) {}

    // 按轮廓分离处理
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
        if (SkScalarNearlyEqual(startDir.fX, -endDir.fX, 1E-4f) &&
            SkScalarNearlyEqual(startDir.fY, -endDir.fY, 1E-4f)) {
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

        // 计算每条曲线的长度
        std::vector<float> curveLengths(numCurves);
        for (size_t i = 0; i < numCurves; ++i) {
            curveLengths[i] = BuildMeasure(curves[i]).getLength();
        }

        // 为闭合路径准备：计算最后一条和第一条曲线之间的圆角
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

        // 开始绘制轮廓
        if (hasFirstArc) {
            // 绘制最后一条和第一条曲线之间的圆角
            dst->moveTo(firstArcCurve.points[0]);
            dst->cubicTo(firstArcCurve.points[1], firstArcCurve.points[2], firstArcCurve.points[3]);
        } else {
            dst->moveTo(curves[0].points[0]);
        }

        // 处理每对相邻曲线
        for (size_t i = 0; i < numCurves - 1; ++i) {
            CurveSegment arcCurve;
            float startLimit = curveLengths[i] * (i == 0 && closed ? 1.0f : 0.5f);
            float endLimit = curveLengths[i + 1] * (i == numCurves - 2 && closed ? 1.0f : 0.5f);

            bool insertArc = BuildCornerCurve(
                    curves[i], startLimit, curves[i + 1], endLimit, fRadius, arcCurve);
            // 绘制当前曲线（可能已被 BuildCornerCurve 修改过端点）
            auto& curCurve = curves[i];
            if (curCurve.verb == SkPath::kLine_Verb) {
                dst->lineTo(curCurve.points[1]);
            } else if (curCurve.verb == SkPath::kQuad_Verb) {
                dst->quadTo(curCurve.points[1], curCurve.points[2]);
            } else if (curCurve.verb == SkPath::kConic_Verb) {
                dst->conicTo(curCurve.points[1], curCurve.points[2], curCurve.conicWeight);
            } else if (curCurve.verb == SkPath::kCubic_Verb) {
                dst->cubicTo(curCurve.points[1], curCurve.points[2], curCurve.points[3]);
            }
            if (insertArc) {
                dst->cubicTo(arcCurve.points[1], arcCurve.points[2], arcCurve.points[3]);
            }
        }

        auto& curCurve = curves.back();
        if (curCurve.verb == SkPath::kLine_Verb) {
            dst->lineTo(curCurve.points[1]);
        } else if (curCurve.verb == SkPath::kQuad_Verb) {
            dst->quadTo(curCurve.points[1], curCurve.points[2]);
        } else if (curCurve.verb == SkPath::kConic_Verb) {
            dst->conicTo(curCurve.points[1], curCurve.points[2], curCurve.conicWeight);
        } else if (curCurve.verb == SkPath::kCubic_Verb) {
            dst->cubicTo(curCurve.points[1], curCurve.points[2], curCurve.points[3]);
        }

        // 闭合路径：处理首尾连接
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