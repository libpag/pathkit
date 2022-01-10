/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** \file SkTypes.h
*/

// Pre-SkUserConfig.h setup.

#if !defined(PK_BUILD_FOR_ANDROID) && !defined(PK_BUILD_FOR_IOS) && !defined(PK_BUILD_FOR_WIN) && \
    !defined(PK_BUILD_FOR_UNIX) && !defined(PK_BUILD_FOR_MAC)

    #ifdef __APPLE__
        #include "TargetConditionals.h"
    #endif

    #if defined(_WIN32) || defined(__SYMBIAN32__)
        #define PK_BUILD_FOR_WIN
    #elif defined(ANDROID) || defined(__ANDROID__)
        #define PK_BUILD_FOR_ANDROID
    #elif defined(linux) || defined(__linux) || defined(__FreeBSD__) || \
          defined(__OpenBSD__) || defined(__sun) || defined(__NetBSD__) || \
          defined(__DragonFly__) || defined(__Fuchsia__) || \
          defined(__GLIBC__) || defined(__GNU__) || defined(__unix__)
        #define PK_BUILD_FOR_UNIX
    #elif TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
        #define PK_BUILD_FOR_IOS
    #else
        #define PK_BUILD_FOR_MAC
    #endif

#endif

#if defined(PK_BUILD_FOR_WIN) && !defined(__clang__)
    #if !defined(PK_RESTRICT)
        #define PK_RESTRICT __restrict
    #endif
    #if !defined(PK_WARN_UNUSED_RESULT)
        #define PK_WARN_UNUSED_RESULT
    #endif
#endif

#if !defined(PK_RESTRICT)
    #define PK_RESTRICT __restrict__
#endif

#if !defined(PK_WARN_UNUSED_RESULT)
    #define PK_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif

#if !defined(PK_CPU_BENDIAN) && !defined(PK_CPU_LENDIAN)
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        #define PK_CPU_BENDIAN
    #elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        #define PK_CPU_LENDIAN
    #elif defined(__sparc) || defined(__sparc__) || \
      defined(_POWER) || defined(__powerpc__) || \
      defined(__ppc__) || defined(__hppa) || \
      defined(__PPC__) || defined(__PPC64__) || \
      defined(_MIPSEB) || defined(__ARMEB__) || \
      defined(__s390__) || \
      (defined(__sh__) && defined(__BIG_ENDIAN__)) || \
      (defined(__ia64) && defined(__BIG_ENDIAN__))
         #define PK_CPU_BENDIAN
    #else
        #define PK_CPU_LENDIAN
    #endif
#endif

#if defined(__i386) || defined(_M_IX86) ||  defined(__x86_64__) || defined(_M_X64)
  #define PK_CPU_X86 1
#endif

/**
 *  PK_CPU_SSE_LEVEL
 *
 *  If defined, PK_CPU_SSE_LEVEL should be set to the highest supported level.
 *  On non-intel CPU this should be undefined.
 */
#define PK_CPU_SSE_LEVEL_SSE1     10
#define PK_CPU_SSE_LEVEL_SSE2     20
#define PK_CPU_SSE_LEVEL_SSE3     30
#define PK_CPU_SSE_LEVEL_SSSE3    31
#define PK_CPU_SSE_LEVEL_SSE41    41
#define PK_CPU_SSE_LEVEL_SSE42    42
#define PK_CPU_SSE_LEVEL_AVX      51
#define PK_CPU_SSE_LEVEL_AVX2     52
#define PK_CPU_SSE_LEVEL_SKX      60

// Are we in GCC/Clang?
#ifndef PK_CPU_SSE_LEVEL
    // These checks must be done in descending order to ensure we set the highest
    // available SSE level.
    #if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
        defined(__AVX512BW__) && defined(__AVX512VL__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SKX
    #elif defined(__AVX2__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_AVX2
    #elif defined(__AVX__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_AVX
    #elif defined(__SSE4_2__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSE42
    #elif defined(__SSE4_1__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSE41
    #elif defined(__SSSE3__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSSE3
    #elif defined(__SSE3__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSE3
    #elif defined(__SSE2__)
        #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSE2
    #endif
#endif

// Are we in VisualStudio?
#ifndef PK_CPU_SSE_LEVEL
    // These checks must be done in descending order to ensure we set the highest
    // available SSE level. 64-bit intel guarantees at least SSE2 support.
    #if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
        defined(__AVX512BW__) && defined(__AVX512VL__)
        #define PK_CPU_SSE_LEVEL        PK_CPU_SSE_LEVEL_SKX
    #elif defined(__AVX2__)
        #define PK_CPU_SSE_LEVEL        PK_CPU_SSE_LEVEL_AVX2
    #elif defined(__AVX__)
        #define PK_CPU_SSE_LEVEL        PK_CPU_SSE_LEVEL_AVX
    #elif defined(_M_X64) || defined(_M_AMD64)
        #define PK_CPU_SSE_LEVEL        PK_CPU_SSE_LEVEL_SSE2
    #elif defined(_M_IX86_FP)
        #if _M_IX86_FP >= 2
            #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSE2
        #elif _M_IX86_FP == 1
            #define PK_CPU_SSE_LEVEL    PK_CPU_SSE_LEVEL_SSE1
        #endif
    #endif
#endif

// ARM defines
#if defined(__arm__) && (!defined(__APPLE__) || !TARGET_IPHONE_SIMULATOR)
    #define PK_CPU_ARM32
#elif defined(__aarch64__)
    #define PK_CPU_ARM64
#endif

// All 64-bit ARM chips have NEON.  Many 32-bit ARM chips do too.
#if !defined(PK_ARM_HAS_NEON) && defined(__ARM_NEON)
    #define PK_ARM_HAS_NEON
#endif

#if defined(__ARM_FEATURE_CRC32)
    #define PK_ARM_HAS_CRC32
#endif


// DLL/.so exports.
#if !defined(PK_IMPLEMENTATION)
    #define PK_IMPLEMENTATION 0
#endif
#if !defined(PK_API)
    #if defined(SKIA_DLL)
        #if defined(_MSC_VER)
            #if PK_IMPLEMENTATION
                #define PK_API __declspec(dllexport)
            #else
                #define PK_API __declspec(dllimport)
            #endif
        #else
            #define PK_API __attribute__((visibility("default")))
        #endif
    #else
        #define PK_API
    #endif
#endif

// PK_SPI is functionally identical to PK_API, but used within src to clarify that it's less stable
#if !defined(PK_SPI)
    #define PK_SPI PK_API
#endif


// IWYU pragma: end_exports

// Post SkUserConfig.h checks and such.
#if !defined(PK_DEBUG) && !defined(PK_RELEASE)
    #define PK_RELEASE 1
#endif

#if defined(PK_DEBUG) && defined(PK_RELEASE)
#  error "cannot define both PK_DEBUG and PK_RELEASE"
#elif !defined(PK_DEBUG) && !defined(PK_RELEASE)
#  error "must define either PK_DEBUG or PK_RELEASE"
#endif

#if defined(PK_CPU_LENDIAN) && defined(PK_CPU_BENDIAN)
#  error "cannot define both PK_CPU_LENDIAN and PK_CPU_BENDIAN"
#elif !defined(PK_CPU_LENDIAN) && !defined(PK_CPU_BENDIAN)
#  error "must define either PK_CPU_LENDIAN or PK_CPU_BENDIAN"
#endif

#if defined(PK_CPU_BENDIAN) && !defined(I_ACKNOWLEDGE_SKIA_DOES_NOT_SUPPORT_BIG_ENDIAN)
    #error "The Skia team is not endian-savvy enough to support big-endian CPUs."
    #error "If you still want to use Skia,"
    #error "please define I_ACKNOWLEDGE_SKIA_DOES_NOT_SUPPORT_BIG_ENDIAN."
#endif

#if !defined(PK_ATTRIBUTE)
#  if defined(__clang__) || defined(__GNUC__)
#    define PK_ATTRIBUTE(attr) __attribute__((attr))
#  else
#    define PK_ATTRIBUTE(attr)
#  endif
#endif

#if !defined(PK_SUPPORT_GPU)
#  define PK_SUPPORT_GPU 1
#endif

#if PK_SUPPORT_GPU
#  if !defined(PK_ENABLE_SKSL)
#    define PK_ENABLE_SKSL
#  endif
#else
#  undef PK_GL
#  undef PK_VULKAN
#  undef PK_METAL
#  undef PK_DAWN
#  undef PK_DIRECT3D
#endif

#if !defined(PkUNREACHABLE)
#  if defined(_MSC_VER) && !defined(__clang__)
#    include <intrin.h>
#    define FAST_FAIL_INVALID_ARG                 5
namespace pk {
// See https://developercommunity.visualstudio.com/content/problem/1128631/code-flow-doesnt-see-noreturn-with-extern-c.html
// for why this is wrapped. Hopefully removable after msvc++ 19.27 is no longer supported.
[[noreturn]] static inline void sk_fast_fail() { __fastfail(FAST_FAIL_INVALID_ARG); }
#define PkUNREACHABLE sk_fast_fail()
}
#else
    #define PkUNREACHABLE __builtin_trap()
#endif
#endif

#if defined(PK_BUILD_FOR_GOOGLE3)
    void SkDebugfForDumpStackTrace(const char* data, void* unused);
    void DumpStackTrace(int skip_count, void w(const char*, void*), void* arg);
#  define PK_DUMP_GOOGLE3_STACK() DumpStackTrace(0, SkDebugfForDumpStackTrace, nullptr)
#else
#  define PK_DUMP_GOOGLE3_STACK()
#endif

#ifndef PK_ABORT
#  ifdef PK_BUILD_FOR_WIN
     // This style lets Visual Studio follow errors back to the source file.
#    define PK_DUMP_LINE_FORMAT "%s(%d)"
#  else
#    define PK_DUMP_LINE_FORMAT "%s:%d"
#  endif
    namespace pk {
#define PK_ABORT(message, ...) \
    do { \
        PK_DUMP_GOOGLE3_STACK(); \
        sk_abort_no_print(); \
    } while (false)
#endif
    }

// If PK_R32_SHIFT is set, we'll use that to choose RGBA or BGRA.
// If not, we'll default to RGBA everywhere except BGRA on Windows.
#if defined(PK_R32_SHIFT)
    static_assert(PK_R32_SHIFT == 0 || PK_R32_SHIFT == 16, "");
#elif defined(PK_BUILD_FOR_WIN)
    #define PK_R32_SHIFT 16
#else
    #define PK_R32_SHIFT 0
#endif

#if defined(PK_B32_SHIFT)
    static_assert(PK_B32_SHIFT == (16-PK_R32_SHIFT), "");
#else
    #define PK_B32_SHIFT (16-PK_R32_SHIFT)
#endif


/**
 * PK_PMCOLOR_BYTE_ORDER can be used to query the byte order of SkPMColor at compile time.
 */
#ifdef PK_CPU_BENDIAN
#  define PK_PMCOLOR_BYTE_ORDER(C0, C1, C2, C3)     \
        (PK_ ## C3 ## 32_SHIFT == 0  &&             \
         PK_ ## C2 ## 32_SHIFT == 8  &&             \
         PK_ ## C1 ## 32_SHIFT == 16 &&             \
         PK_ ## C0 ## 32_SHIFT == 24)
#else
#  define PK_PMCOLOR_BYTE_ORDER(C0, C1, C2, C3)     \
        (PK_ ## C0 ## 32_SHIFT == 0  &&             \
         PK_ ## C1 ## 32_SHIFT == 8  &&             \
         PK_ ## C2 ## 32_SHIFT == 16 &&             \
         PK_ ## C3 ## 32_SHIFT == 24)
#endif

#if !defined(PK_UNUSED)
#  if !defined(__clang__) && defined(_MSC_VER)
#    define PK_UNUSED __pragma(warning(suppress:4189))
#  else
#    define PK_UNUSED PK_ATTRIBUTE(unused)
#  endif
#endif

#if !defined(PK_MAYBE_UNUSED)
#  if defined(__clang__) || defined(__GNUC__)
#    define PK_MAYBE_UNUSED [[maybe_unused]]
#  else
#    define PK_MAYBE_UNUSED
#  endif
#endif

/**
 * If your judgment is better than the compiler's (i.e. you've profiled it),
 * you can use PK_ALWAYS_INLINE to force inlining. E.g.
 *     inline void someMethod() { ... }             // may not be inlined
 *     PK_ALWAYS_INLINE void someMethod() { ... }   // should always be inlined
 */
#if !defined(PK_ALWAYS_INLINE)
#  if defined(PK_BUILD_FOR_WIN)
#    define PK_ALWAYS_INLINE __forceinline
#  else
#    define PK_ALWAYS_INLINE PK_ATTRIBUTE(always_inline) inline
#  endif
#endif

#if PK_CPU_SSE_LEVEL >= PK_CPU_SSE_LEVEL_SSE1
    #define PK_PREFETCH(ptr) _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#elif defined(__GNUC__)
    #define PK_PREFETCH(ptr) __builtin_prefetch(ptr)
#else
    #define PK_PREFETCH(ptr)
#endif

#ifndef PK_PRINTF_LIKE
    #if defined(__clang__) || defined(__GNUC__)
        #define PK_PRINTF_LIKE(A, B) __attribute__((format(printf, (A), (B))))
    #else
        #define PK_PRINTF_LIKE(A, B)
    #endif
#endif

    namespace pk {
    /** Called internally if we hit an unrecoverable error.
        The platform implementation must not return, but should either throw
        an exception or otherwise exit.
    */
    [[noreturn]] PK_API extern void sk_abort_no_print(void);

#ifndef SkDebugf
    PK_API void SkDebugf(const char format[], ...) PK_PRINTF_LIKE(1, 2);
#endif
    }

#define PkASSERT_RELEASE(cond) \
        static_cast<void>( (cond) ? (void)0 : []{ PK_ABORT("assert(%s)", #cond); }() )

    #define PkASSERT(cond)            static_cast<void>(0)
    #define PkASSERTF(cond, fmt, ...) static_cast<void>(0)
    #define PkDEBUGFAIL(message)
    #define PkDEBUGCODE(...)
    #define PkDEBUGF(...)

    // unlike PkASSERT, this macro executes its condition in the non-debug build.
    // The if is present so that this can be used with functions marked PK_WARN_UNUSED_RESULT.
    #define PkAssertResult(cond)         if (cond) {} do {} while(false)

////////////////////////////////////////////////////////////////////////////////
namespace pk {
/** Fast type for unsigned 8 bits. Use for parameter passing and local
    variables, not for storage
*/
typedef unsigned U8CPU;

/** Fast type for unsigned 16 bits. Use for parameter passing and local
    variables, not for storage
*/
typedef unsigned U16CPU;

/** @return false or true based on the condition
*/
template <typename T> static constexpr bool SkToBool(const T& x) {
    return 0 != x;  // NOLINT(modernize-use-nullptr)
}

static constexpr int32_t PK_MaxS32 = INT32_MAX;
static constexpr int32_t PK_MinS32 = -PK_MaxS32;
static constexpr int32_t PK_NaN32  = INT32_MIN;
static constexpr int64_t PK_MaxS64 = INT64_MAX;

////////////////////////////////////////////////////////////////////////////////

/** @return the number of entries in an array (not a pointer)
*/
template <typename T, size_t N> char (&SkArrayCountHelper(T (&array)[N]))[N];
#define PK_ARRAY_COUNT(array) (sizeof(SkArrayCountHelper(array)))

////////////////////////////////////////////////////////////////////////////////

template <typename T> static constexpr T SkAlign4(T x) { return (x + 3) >> 2 << 2; }
template <typename T> static constexpr bool SkIsAlign2(T x) { return 0 == (x & 1); }

template <typename T> static inline T SkTAbs(T value) {
    if (value < 0) {
        value = -value;
    }
    return value;
}

}  // namespace pk
