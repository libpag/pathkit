/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkPaint.h"

namespace pk {
class SkPaintPriv {
public:
    static SkScalar ComputeResScaleForStroking(const SkMatrix&);
};
}  // namespace pk
