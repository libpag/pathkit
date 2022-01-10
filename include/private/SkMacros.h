/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

// Can be used to bracket data types that must be dense, e.g. hash keys.
#if defined(__clang__)  // This should work on GCC too, but GCC diagnostic pop didn't seem to work!
    #define PK_BEGIN_REQUIRE_DENSE _Pragma("GCC diagnostic push") \
                                   _Pragma("GCC diagnostic error \"-Wpadded\"")
    #define PK_END_REQUIRE_DENSE   _Pragma("GCC diagnostic pop")
#else
    #define PK_BEGIN_REQUIRE_DENSE
    #define PK_END_REQUIRE_DENSE
#endif

#define PK_INIT_TO_AVOID_WARNING    = 0
