/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrWangsFormula_DEFINED
#define GrWangsFormula_DEFINED

#include "include/core/SkPoint.h"
#include "src/gpu/GrVx.h"
#include "src/gpu/tessellate/GrVectorXform.h"

namespace pk {
// Wang's formula gives the minimum number of evenly spaced (in the parametric sense) line segments
// that a bezier curve must be chopped into in order to guarantee all lines stay within a distance
// of "1/precision" pixels from the true curve. Its definition for a bezier curve of degree "n" is
// as follows:
//
//     maxLength = max([length(p[i+2] - 2p[i+1] + p[i]) for (0 <= i <= n-2)])
//     numParametricSegments = sqrt(maxLength * precision * n*(n - 1)/8)
//
// (Goldman, Ron. (2003). 5.6.3 Wang's Formula. "Pyramid Algorithms: A Dynamic Programming Approach
// to Curves and Surfaces for Geometric Modeling". Morgan Kaufmann Publishers.)
namespace GrWangsFormula {
template <int Degree> constexpr float length_term_pow2(float precision) {
    return ((Degree * Degree) * ((Degree - 1) * (Degree - 1)) / 64.f) * (precision * precision);
}

// Returns nextlog2(sqrt(sqrt(x))):
//
//   log2(sqrt(sqrt(x))) == log2(x^(1/4)) == log2(x)/4 == log2(x)/log2(16) == log16(x)
//
PK_ALWAYS_INLINE static int nextlog16(float x) { return (pk_float_nextlog2(x) + 3) >> 2; }

// Returns Wang's formula, raised to the 4th power, specialized for a quadratic curve.
// Returns Wang's formula, raised to the 4th power, specialized for a cubic curve.
PK_ALWAYS_INLINE static float cubic_pow4(float precision,
                                         const SkPoint pts[],
                                         const GrVectorXform& vectorXform = GrVectorXform()) {
    using grvx::float4;
    float4 p01 = float4::Load(pts);
    float4 p12 = float4::Load(pts + 1);
    float4 p23 = float4::Load(pts + 2);
    float4 v = grvx::fast_madd<4>(-2, p12, p01) + p23;
    v = vectorXform(v);
    float4 vv = v * v;
    return std::max(vv[0] + vv[1], vv[2] + vv[3]) * length_term_pow2<3>(precision);
}

// Returns the log2 value of Wang's formula specialized for a cubic curve, rounded up to the next
// int.
PK_ALWAYS_INLINE static int cubic_log2(float precision,
                                       const SkPoint pts[],
                                       const GrVectorXform& vectorXform = GrVectorXform()) {
    // nextlog16(x) == ceil(log2(sqrt(sqrt(x))))
    return nextlog16(cubic_pow4(precision, pts, vectorXform));
}
}  // namespace GrWangsFormula
}  // namespace pk

#endif
