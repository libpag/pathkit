/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/geometry/GrPathUtils.h"
#include "src/gpu/geometry/GrWangsFormula.h"

namespace pk {
static float tolerance_to_wangs_precision(float srcTol) {
    // The GrPathUtil API defines tolerance as the max distance the linear segment can be from
    // the real curve. Wang's formula guarantees the linear segments will be within 1/precision
    // of the true curve, so precision = 1/srcTol
    return 1.f / srcTol;
}

uint32_t max_bezier_vertices(uint32_t chopCount) {
    static constexpr uint32_t kMaxChopsPerCurve = 10;
    static_assert((1 << kMaxChopsPerCurve) == GrPathUtils::kMaxPointsPerCurve);
    return 1 << std::min(chopCount, kMaxChopsPerCurve);
}

uint32_t GrPathUtils::cubicPointCount(const SkPoint points[], SkScalar tol) {
    return max_bezier_vertices(
            GrWangsFormula::cubic_log2(tolerance_to_wangs_precision(tol), points));
}
}  // namespace pk
