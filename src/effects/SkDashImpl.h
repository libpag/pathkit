/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "src/core/SkPathEffectBase.h"

namespace pk {
class SkDashImpl : public SkPathEffectBase {
public:
    SkDashImpl(const SkScalar intervals[], int count, SkScalar phase);

protected:
    ~SkDashImpl() override;
    bool onFilterPath(SkPath* dst, const SkPath& src, SkStrokeRec*, const SkRect*,
                      const SkMatrix&) const override;

    bool onAsPoints(PointData* results, const SkPath& src, const SkStrokeRec&, const SkMatrix&,
                    const SkRect*) const override;

    DashType onAsADash(DashInfo* info) const override;

private:
    bool computeFastBounds(SkRect* bounds) const override {
        // Dashing a path returns a subset of the input path so just return true and leave
        // bounds unmodified
        return true;
    }

    SkScalar*   fIntervals;
    int32_t     fCount;
    SkScalar    fPhase;
    // computed from phase

    SkScalar    fInitialDashLength;
    int32_t     fInitialDashIndex;
    SkScalar    fIntervalLength;

    using INHERITED = SkPathEffectBase;
};
}  // namespace pk
