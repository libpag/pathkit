/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkPoint.h"

namespace pk {
struct PK_API SkPoint3 {
    SkScalar fX, fY, fZ;

    SkScalar x() const { return fX; }
    SkScalar y() const { return fY; }
    SkScalar z() const { return fZ; }

    void set(SkScalar x, SkScalar y, SkScalar z) { fX = x; fY = y; fZ = z; }

    friend bool operator==(const SkPoint3& a, const SkPoint3& b) {
        return a.fX == b.fX && a.fY == b.fY && a.fZ == b.fZ;
    }

    friend bool operator!=(const SkPoint3& a, const SkPoint3& b) {
        return !(a == b);
    }
};
}  // namespace pk
