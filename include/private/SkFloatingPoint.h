/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <float.h>
#include <math.h>
#include <cmath>
#include <cstring>
#include <limits>

#include "include/core/SkTypes.h"
#include "include/private/SkFloatBits.h"
#include "include/private/SkSafe_math.h"

#if defined(PK_LEGACY_FLOAT_RSQRT)
#if PK_CPU_SSE_LEVEL >= PK_CPU_SSE_LEVEL_SSE1
    #include <xmmintrin.h>
#elif defined(PK_ARM_HAS_NEON)
    #include <arm_neon.h>
#endif
#endif

// For _POSIX_VERSION
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#endif

namespace pk {
constexpr float PK_FloatSqrt2 = 1.41421356f;
constexpr float PK_FloatPI    = 3.14159265f;

// C++98 cmath std::pow seems to be the earliest portable way to get float pow.
// However, on Linux including cmath undefines isfinite.
// http://gcc.gnu.org/bugzilla/show_bug.cgi?id=14608
static inline float pk_float_pow(float base, float exp) {
    return powf(base, exp);
}

#define pk_float_sqrt(x)        sqrtf(x)
#define pk_float_sin(x)         sinf(x)
#define pk_float_cos(x)         cosf(x)
#define pk_float_tan(x)         tanf(x)
#define pk_float_floor(x)       floorf(x)
#define pk_float_ceil(x)        ceilf(x)
#define pk_float_trunc(x)       truncf(x)
#ifdef PK_BUILD_FOR_MAC
    #define pk_float_acos(x)    static_cast<float>(acos(x))
    #define pk_float_asin(x)    static_cast<float>(asin(x))
#else
    #define pk_float_acos(x)    acosf(x)
    #define pk_float_asin(x)    asinf(x)
#endif
#define pk_float_atan2(y,x)     atan2f(y,x)
#define pk_float_abs(x)         fabsf(x)
#define pk_float_copysign(x, y) copysignf(x, y)
#define pk_float_mod(x,y)       fmodf(x,y)
#define pk_float_exp(x)         expf(x)
#define pk_float_log(x)         logf(x)

// can't find log2f on android, but maybe that just a tool bug?
#ifdef PK_BUILD_FOR_ANDROID
    static inline float pk_float_log2(float x) {
        const double inv_ln_2 = 1.44269504088896;
        return (float)(log(x) * inv_ln_2);
    }
#else
    #define pk_float_log2(x)        log2f(x)
#endif

static inline bool sk_float_isfinite(float x) {
    return SkFloatBits_IsFinite(SkFloat2Bits(x));
}

static inline bool sk_floats_are_finite(float a, float b) {
    return sk_float_isfinite(a) && sk_float_isfinite(b);
}

static inline bool sk_floats_are_finite(const float array[], int count) {
    float prod = 0;
    for (int i = 0; i < count; ++i) {
        prod *= array[i];
    }
    // At this point, prod will either be NaN or 0
    return prod == 0;   // if prod is NaN, this check will return false
}

static inline bool sk_float_isnan(float x) {
    return !(x == x);
}

#define PK_MaxS32FitsInFloat    2147483520
#define PK_MinS32FitsInFloat    -PK_MaxS32FitsInFloat

#define PK_MaxS64FitsInFloat    (PK_MaxS64 >> (63-24) << (63-24))   // 0x7fffff8000000000
#define PK_MinS64FitsInFloat    -PK_MaxS64FitsInFloat

/**
 *  Return the closest int for the given float. Returns PK_MaxS32FitsInFloat for NaN.
 */
static inline int pk_float_saturate2int(float x) {
    x = x < PK_MaxS32FitsInFloat ? x : PK_MaxS32FitsInFloat;
    x = x > PK_MinS32FitsInFloat ? x : PK_MinS32FitsInFloat;
    return (int)x;
}

#define pk_float_floor2int(x)   pk_float_saturate2int(pk_float_floor(x))
#define pk_float_round2int(x)   pk_float_saturate2int(pk_float_floor((x) + 0.5f))
#define pk_float_ceil2int(x)    pk_float_saturate2int(pk_float_ceil(x))


// Cast double to float, ignoring any warning about too-large finite values being cast to float.
// Clang thinks this is undefined, but it's actually implementation defined to return either
// the largest float or infinity (one of the two bracketing representable floats).  Good enough!
PK_ATTRIBUTE(no_sanitize("float-cast-overflow"))
static inline float pk_double_to_float(double x) {
    return static_cast<float>(x);
}

#define PK_FloatNaN                 std::numeric_limits<float>::quiet_NaN()
#define PK_FloatInfinity            (+std::numeric_limits<float>::infinity())
#define PK_FloatNegativeInfinity    (-std::numeric_limits<float>::infinity())

// Returns the log2 of the provided value, were that value to be rounded up to the next power of 2.
// Returns 0 if value <= 0:
// Never returns a negative number, even if value is NaN.
//
//     sk_float_nextlog2((-inf..1]) -> 0
//     sk_float_nextlog2((1..2]) -> 1
//     sk_float_nextlog2((2..4]) -> 2
//     sk_float_nextlog2((4..8]) -> 3
//     ...
static inline int pk_float_nextlog2(float x) {
    uint32_t bits = (uint32_t)SkFloat2Bits(x);
    bits += (1u << 23) - 1u;  // Increment the exponent for non-powers-of-2.
    int exp = ((int32_t)bits >> 23) - 127;
    return exp & ~(exp >> 31);  // Return 0 for negative or denormalized floats, and exponents < 0.
}

// This is the number of significant digits we can print in a string such that when we read that
// string back we get the floating point number we expect.  The minimum value C requires is 6, but
// most compilers support 9
#ifdef FLT_DECIMAL_DIG
#define PK_FLT_DECIMAL_DIG FLT_DECIMAL_DIG
#else
#define PK_FLT_DECIMAL_DIG 9
#endif

// IEEE defines how float divide behaves for non-finite values and zero-denoms, but C does not
// so we have a helper that suppresses the possible undefined-behavior warnings.

PK_ATTRIBUTE(no_sanitize("float-divide-by-zero"))
static inline float sk_ieee_float_divide(float numer, float denom) {
    return numer / denom;
}

PK_ATTRIBUTE(no_sanitize("float-divide-by-zero"))
static inline double sk_ieee_double_divide(double numer, double denom) {
    return numer / denom;
}

// While we clean up divide by zero, we'll replace places that do divide by zero with this TODO.
static inline float sk_ieee_float_divide_TODO_IS_DIVIDE_BY_ZERO_SAFE_HERE(float n, float d) {
    return sk_ieee_float_divide(n,d);
}
}  // namespace pk
