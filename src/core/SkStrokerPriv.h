/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#pragma once

#include "src/core/SkStroke.h"

namespace pk {
class SkStrokerPriv {
public:
    typedef void (*CapProc)(SkPath* path,
                            const SkPoint& pivot,
                            const SkVector& normal,
                            const SkPoint& stop,
                            SkPath* otherPath);

    typedef void (*JoinProc)(SkPath* outer, SkPath* inner,
                             const SkVector& beforeUnitNormal,
                             const SkPoint& pivot,
                             const SkVector& afterUnitNormal,
                             SkScalar radius, SkScalar invMiterLimit,
                             bool prevIsLine, bool currIsLine);

    static CapProc  CapFactory(SkPaint::Cap);
    static JoinProc JoinFactory(SkPaint::Join);
};
}  // namespace pk
