#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkStrokeRec.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/pathops/SkPathOps.h"

using namespace pk;

static void TestTightBounds() {
    constexpr auto side = 300.f;
    SkPath path;
    path.moveTo(0.f, 0.f);
    path.cubicTo(side * 0.25f, side, side * 0.75f, -side, side, 0.f);
    path.cubicTo(side * 1.25f, side, side * 1.75f, -side, side * 2.f, 0.f);
    const auto bounds = path.getBounds();
    const auto tightBounds = path.computeTightBounds();
    printf("TestTightBounds: bounds{%f, %f, %f, %f}, tightBounds{%f, %f, %f, %f}\n",
           bounds.x(),
           bounds.y(),
           bounds.width(),
           bounds.height(),
           tightBounds.x(),
           tightBounds.y(),
           tightBounds.width(),
           tightBounds.height());
}

int main() {
    SkPath skPath{};
    skPath.addRect(SkRect::MakeXYWH(100, 100, 100, 100));
    Op(skPath, skPath, SkPathOp::kUnion_SkPathOp, &skPath);
    float intervals[] = {1, 2, 3, 4};
    auto dashEffect = SkDashPathEffect::Make(intervals, 4, 1);
    SkStrokeRec rec(SkStrokeRec::kHairline_InitStyle);
    dashEffect->filterPath(&skPath, skPath, &rec, nullptr);
    TestTightBounds();
    return 0;
}
