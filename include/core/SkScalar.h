/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/private/SkFloatingPoint.h"

namespace pk {
#undef PK_SCALAR_IS_FLOAT
#define PK_SCALAR_IS_FLOAT  1

typedef float SkScalar;

#define PK_Scalar1                  1.0f
#define PK_ScalarHalf               0.5f
#define PK_ScalarSqrt2              PK_FloatSqrt2
#define PK_ScalarPI                 PK_FloatPI
#define PK_ScalarRoot2Over2         0.707106781f
#define PK_ScalarMax                3.402823466e+38f
#define PK_ScalarInfinity           PK_FloatInfinity
#define PK_ScalarNegativeInfinity   PK_FloatNegativeInfinity
#define PK_ScalarNaN                PK_FloatNaN

#define PkScalarFloorToScalar(x)    pk_float_floor(x)
#define PkScalarCeilToScalar(x)     pk_float_ceil(x)
#define PkScalarRoundToScalar(x)    pk_float_floor((x) + 0.5f)
#define PkScalarTruncToScalar(x)    pk_float_trunc(x)

#define PkScalarFloorToInt(x)       pk_float_floor2int(x)
#define PkScalarCeilToInt(x)        pk_float_ceil2int(x)
#define PkScalarRoundToInt(x)       pk_float_round2int(x)

#define PkScalarAbs(x)              pk_float_abs(x)
#define PkScalarCopySign(x, y)      pk_float_copysign(x, y)
#define PkScalarMod(x, y)           pk_float_mod(x,y)
#define PkScalarSqrt(x)             pk_float_sqrt(x)
#define PkScalarPow(b, e)           pk_float_pow(b, e)

#define PkScalarSin(radians)        (float)pk_float_sin(radians)
#define PkScalarCos(radians)        (float)pk_float_cos(radians)
#define PkScalarTan(radians)        (float)pk_float_tan(radians)
#define PkScalarASin(val)           (float)pk_float_asin(val)
#define PkScalarACos(val)           (float)pk_float_acos(val)
#define PkScalarATan2(y, x)         (float)pk_float_atan2(y,x)
#define PkScalarExp(x)              (float)pk_float_exp(x)
#define PkScalarLog(x)              (float)pk_float_log(x)
#define PkScalarLog2(x)             (float)pk_float_log2(x)

//////////////////////////////////////////////////////////////////////////////////////////////////

#define PkIntToScalar(x)        static_cast<SkScalar>(x)
#define PkIntToFloat(x)         static_cast<float>(x)
#define PkScalarTruncToInt(x)   pk_float_saturate2int(x)

#define PkScalarToFloat(x)      static_cast<float>(x)
#define PkFloatToScalar(x)      static_cast<SkScalar>(x)
#define PkScalarToDouble(x)     static_cast<double>(x)
#define PkDoubleToScalar(x)     pk_double_to_float(x)

#define PK_ScalarMin            (-PK_ScalarMax)

static inline bool SkScalarIsNaN(SkScalar x) { return x != x; }

/** Returns true if x is not NaN and not infinite
 */
static inline bool SkScalarIsFinite(SkScalar x) { return sk_float_isfinite(x); }

static inline bool SkScalarsAreFinite(SkScalar a, SkScalar b) {
    return sk_floats_are_finite(a, b);
}

static inline bool SkScalarsAreFinite(const SkScalar array[], int count) {
    return sk_floats_are_finite(array, count);
}

/**
 *  Variant of SkScalarRoundToInt, that performs the rounding step (adding 0.5) explicitly using
 *  double, to avoid possibly losing the low bit(s) of the answer before calling floor().
 *
 *  This routine will likely be slower than SkScalarRoundToInt(), and should only be used when the
 *  extra precision is known to be valuable.
 *
 *  In particular, this catches the following case:
 *      SkScalar x = 0.49999997;
 *      int ix = SkScalarRoundToInt(x);
 *      ix = SkDScalarRoundToInt(x);
 */
static inline int SkDScalarRoundToInt(SkScalar x) {
    double xx = x;
    xx += 0.5;
    return (int)floor(xx);
}

/** Returns the fractional part of the scalar. */
static inline SkScalar SkScalarFraction(SkScalar x) {
    return x - PkScalarTruncToScalar(x);
}

static inline SkScalar SkScalarSquare(SkScalar x) { return x * x; }

#define PkScalarInvert(x)           sk_ieee_float_divide_TODO_IS_DIVIDE_BY_ZERO_SAFE_HERE(PK_Scalar1, (x))
#define PkScalarAve(a, b)           (((a) + (b)) * PK_ScalarHalf)
#define PkScalarHalf(a)             ((a) * PK_ScalarHalf)

#define PkDegreesToRadians(degrees) ((degrees) * (PK_ScalarPI / 180))
#define PkRadiansToDegrees(radians) ((radians) * (180 / PK_ScalarPI))

static inline bool SkScalarIsInt(SkScalar x) {
    return x == PkScalarFloorToScalar(x);
}

/**
 *  Returns -1 || 0 || 1 depending on the sign of value:
 *  -1 if x < 0
 *   0 if x == 0
 *   1 if x > 0
 */
static inline int SkScalarSignAsInt(SkScalar x) {
    return x < 0 ? -1 : (x > 0);
}

// Scalar result version of above
static inline SkScalar SkScalarSignAsScalar(SkScalar x) {
    return x < 0 ? -PK_Scalar1 : ((x > 0) ? PK_Scalar1 : 0);
}

#define PK_ScalarNearlyZero         (PK_Scalar1 / (1 << 12))

static inline bool SkScalarNearlyZero(SkScalar x,
                                      SkScalar tolerance = PK_ScalarNearlyZero) {
    return PkScalarAbs(x) <= tolerance;
}

static inline bool SkScalarNearlyEqual(SkScalar x, SkScalar y,
                                       SkScalar tolerance = PK_ScalarNearlyZero) {
    return PkScalarAbs(x-y) <= tolerance;
}

static inline float SkScalarSinSnapToZero(SkScalar radians) {
    float v = PkScalarSin(radians);
    return SkScalarNearlyZero(v) ? 0.0f : v;
}

static inline float SkScalarCosSnapToZero(SkScalar radians) {
    float v = PkScalarCos(radians);
    return SkScalarNearlyZero(v) ? 0.0f : v;
}

/** Linearly interpolate between A and B, based on t.
    If t is 0, return A
    If t is 1, return B
    else interpolate.
    t must be [0..PK_Scalar1]
*/
static inline SkScalar SkScalarInterp(SkScalar A, SkScalar B, SkScalar t) {
    return A + (B - A) * t;
}

/** Interpolate along the function described by (keys[length], values[length])
    for the passed searchKey. SearchKeys outside the range keys[0]-keys[Length]
    clamp to the min or max value. This function assumes the number of pairs
    (length) will be small and a linear search is used.

    Repeated keys are allowed for discontinuous functions (so long as keys is
    monotonically increasing). If key is the value of a repeated scalar in
    keys the first one will be used.
*/
SkScalar SkScalarInterpFunc(SkScalar searchKey, const SkScalar keys[],
                            const SkScalar values[], int length);

/*
 *  Helper to compare an array of scalars.
 */
static inline bool SkScalarsEqual(const SkScalar a[], const SkScalar b[], int n) {
    for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}
}  // namespace pk
