#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkStrokeRec.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/pathops/SkPathOps.h"

using namespace pk;

int main() {
    SkPath skPath{};
    skPath.addRect(SkRect::MakeXYWH(100, 100, 100, 100));
    Op(skPath, skPath, SkPathOp::kUnion_SkPathOp, &skPath);
    float intervals[] = {1, 2, 3, 4};
    auto dashEffect = SkDashPathEffect::Make(intervals, 4, 1);
    SkStrokeRec rec(SkStrokeRec::kHairline_InitStyle);
    dashEffect->filterPath(&skPath, skPath, &rec, nullptr);
    return 0;
}
