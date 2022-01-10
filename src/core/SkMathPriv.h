/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkMath.h"

#if defined(_MSC_VER)
#include <stdlib.h>
#endif
#if defined(PK_BUILD_FOR_WIN)
#include <intrin.h>
#endif

namespace pk {
//! Returns the number of leading zero bits (0...32)
// From Hacker's Delight 2nd Edition
constexpr int SkCLZ_portable(uint32_t x) {
    int n = 32;
    uint32_t y = x >> 16; if (y != 0) {n -= 16; x = y;}
             y = x >>  8; if (y != 0) {n -=  8; x = y;}
             y = x >>  4; if (y != 0) {n -=  4; x = y;}
             y = x >>  2; if (y != 0) {n -=  2; x = y;}
             y = x >>  1; if (y != 0) {return n - 2;}
    return n - x;
}

static_assert(32 == SkCLZ_portable(0), "");
static_assert(31 == SkCLZ_portable(1), "");
static_assert( 1 == SkCLZ_portable(1 << 30), "");
static_assert( 1 == SkCLZ_portable((1 << 30) | (1 << 24) | 1), "");
static_assert( 0 == SkCLZ_portable(~0U), "");

#if defined(PK_BUILD_FOR_WIN)
    static inline int SkCLZ(uint32_t mask) {
        if (mask) {
            unsigned long index = 0;
            _BitScanReverse(&index, mask);
            // Suppress this bogus /analyze warning. The check for non-zero
            // guarantees that _BitScanReverse will succeed.
            #pragma warning(suppress : 6102) // Using 'index' from failed function call
            return index ^ 0x1F;
        } else {
            return 32;
        }
    }
#elif defined(PK_CPU_ARM32) || defined(__GNUC__) || defined(__clang__)
    static inline int SkCLZ(uint32_t mask) {
        // __builtin_clz(0) is undefined, so we have to detect that case.
        return mask ? __builtin_clz(mask) : 32;
    }
#else
    static inline int SkCLZ(uint32_t mask) {
        return SkCLZ_portable(mask);
    }
#endif

//! Returns the number of trailing zero bits (0...32)
// From Hacker's Delight 2nd Edition
constexpr int SkCTZ_portable(uint32_t x) {
    return 32 - SkCLZ_portable(~x & (x - 1));
}

static_assert(32 == SkCTZ_portable(0), "");
static_assert( 0 == SkCTZ_portable(1), "");
static_assert(30 == SkCTZ_portable(1 << 30), "");
static_assert( 2 == SkCTZ_portable((1 << 30) | (1 << 24) | (1 << 2)), "");
static_assert( 0 == SkCTZ_portable(~0U), "");

#if defined(PK_BUILD_FOR_WIN)
    static inline int SkCTZ(uint32_t mask) {
        if (mask) {
            unsigned long index = 0;
            _BitScanForward(&index, mask);
            // Suppress this bogus /analyze warning. The check for non-zero
            // guarantees that _BitScanReverse will succeed.
            #pragma warning(suppress : 6102) // Using 'index' from failed function call
            return index;
        } else {
            return 32;
        }
    }
#elif defined(PK_CPU_ARM32) || defined(__GNUC__) || defined(__clang__)
    static inline int SkCTZ(uint32_t mask) {
        // __builtin_ctz(0) is undefined, so we have to detect that case.
        return mask ? __builtin_ctz(mask) : 32;
    }
#else
    static inline int SkCTZ(uint32_t mask) {
        return SkCTZ_portable(mask);
    }
#endif

/**
 *  Returns the log2 of the specified value, were that value to be rounded up
 *  to the next power of 2. It is undefined to pass 0. Examples:
 *  SkNextLog2(1) -> 0
 *  SkNextLog2(2) -> 1
 *  SkNextLog2(3) -> 2
 *  SkNextLog2(4) -> 2
 *  SkNextLog2(5) -> 3
 */
static inline int SkNextLog2(uint32_t value) {
    return 32 - SkCLZ(value - 1);
}

// conservative check. will return false for very large values that "could" fit
template <typename T> static inline bool SkFitsInFixed(T x) {
    return SkTAbs(x) <= 32767.0f;
}
}  // namespace pk
