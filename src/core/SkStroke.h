/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/private/SkTo.h"
#include "src/core/SkStrokerPriv.h"

namespace pk {

// Encapsulates stroke parameters (miter limit, cap, join)
struct SkStrokeParams {
    SkScalar miterLimit;
    SkPaint::Cap cap;
    SkPaint::Join join;

    SkStrokeParams(SkScalar ml, SkPaint::Cap c, SkPaint::Join j)
            : miterLimit(ml), cap(c), join(j) {}

    SkStrokeParams();

    bool operator==(const SkStrokeParams& other) const {
        return miterLimit == other.miterLimit && cap == other.cap && join == other.join;
    }

    bool operator!=(const SkStrokeParams& other) const { return !(*this == other); }
};

/** \class SkStroke
    SkStroke is the utility class that constructs paths by stroking
    geometries (lines, rects, ovals, roundrects, paths). This is
    invoked when a geometry or text is drawn in a canvas with the
    kStroke_Mask bit set in the paint.
*/
class SkStroke {
public:
    SkStroke();

    SkPaint::Cap    getCap() const { return (SkPaint::Cap)fCap; }
    void        setCap(SkPaint::Cap);

    SkPaint::Join   getJoin() const { return (SkPaint::Join)fJoin; }
    void        setJoin(SkPaint::Join);

    void    setMiterLimit(SkScalar);
    void    setWidth(SkScalar);

    bool    getDoFill() const { return SkToBool(fDoFill); }
    void    setDoFill(bool doFill) { fDoFill = SkToU8(doFill); }

    /**
     *  ResScale is the "intended" resolution for the output.
     *      Default is 1.0.
     *      Larger values (res > 1) indicate that the result should be more precise, since it will
     *          be zoomed up, and small errors will be magnified.
     *      Smaller values (0 < res < 1) indicate that the result can be less precise, since it will
     *          be zoomed down, and small errors may be invisible.
     */
    SkScalar getResScale() const { return fResScale; }
    void setResScale(SkScalar rs) {
        fResScale = rs;
    }

    /**
     *  Stroke the specified rect, winding it in the specified direction..
     */
    void    strokeRect(const SkRect& rect, SkPath* result,
                       SkPathDirection = SkPathDirection::kCW) const;
    void    strokePath(const SkPath& path, SkPath*) const;

    /**
     * Apply stroke with multiple parameters to a path.
     * Each segment can have different stroke parameters (miter limit, cap, join).
     * If params are fewer than segments, they will be cycled through.
     * 
     * @param src Source path to stroke
     * @param dst Output stroked path (must not be null)
     * @param width Stroke width (must be >= 0)
     * @param params Vector of stroke parameters (must not be empty)
     * @param resScale Resolution scale factor (must be > 0)
     * @return true if successful, false on error
     */
    static bool StrokePathWithMultiParams(const SkPath& src,
                                          SkPath* dst,
                                          SkScalar width,
                                          const std::vector<SkStrokeParams>& params,
                                          SkScalar resScale);

    ////////////////////////////////////////////////////////////////

private:
    SkScalar    fWidth, fMiterLimit;
    SkScalar    fResScale;
    uint8_t     fCap, fJoin;
    bool        fDoFill;

    friend class SkPaint;
};
}  // namespace pk
