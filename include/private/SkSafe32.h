/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkTypes.h"

namespace pk {
static constexpr int32_t Sk64_pin_to_s32(int64_t x) {
    return x < PK_MinS32 ? PK_MinS32 : (x > PK_MaxS32 ? PK_MaxS32 : (int32_t)x);
}

static constexpr int32_t Sk32_sat_add(int32_t a, int32_t b) {
    return Sk64_pin_to_s32((int64_t)a + (int64_t)b);
}

static constexpr int32_t Sk32_sat_sub(int32_t a, int32_t b) {
    return Sk64_pin_to_s32((int64_t)a - (int64_t)b);
}
}  // namespace pk
