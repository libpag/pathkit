/*
 * Copyright 2025 The PathKit Authors
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkStrokeParams_DEFINED
#define SkStrokeParams_DEFINED

#include <vector>
#include "include/core/SkPaint.h"
#include "include/core/SkScalar.h"
#include "src/core/SkPaintDefaults.h"

namespace pk {

// Encapsulates stroke parameters (miter limit, cap, join)
struct SkStrokeParams {
    SkScalar miterLimit;
    SkPaint::Cap cap;
    SkPaint::Join join;

    SkStrokeParams(SkScalar ml, SkPaint::Cap c, SkPaint::Join j)
            : miterLimit(ml), cap(c), join(j) {}

    SkStrokeParams()
            : miterLimit(PkPaintDefaults_MiterLimit)
            , cap(SkPaint::kDefault_Cap)
            , join(SkPaint::kDefault_Join) {}

    bool operator==(const SkStrokeParams& other) const {
        return miterLimit == other.miterLimit && cap == other.cap && join == other.join;
    }

    bool operator!=(const SkStrokeParams& other) const { return !(*this == other); }
};

bool StrokePath(const SkPath& src,
                SkPath* dst,
                SkScalar width,
                const std::vector<SkStrokeParams>& params,
                SkScalar resScale);

}  // namespace pk

#endif  // SkStrokeParams_DEFINED
