/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkPath.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"

namespace pk {
class SkPath;
class SkStrokeRec;

class SkPathEffectBase : public SkPathEffect {
public:
    SkPathEffectBase() {}

    /** \class PointData

        PointData aggregates all the information needed to draw the point
        primitives returned by an 'asPoints' call.
    */
    class PointData {
    public:
        PointData()
            : fFlags(0)
            , fPoints(nullptr)
            , fNumPoints(0) {
            fSize.set(PK_Scalar1, PK_Scalar1);
            // 'asPoints' needs to initialize/fill-in 'fClipRect' if it sets
            // the kUseClip flag
        }
        ~PointData() {
            delete [] fPoints;
        }

        // TODO: consider using passed-in flags to limit the work asPoints does.
        // For example, a kNoPath flag could indicate don't bother generating
        // stamped solutions.

        // Currently none of these flags are supported.
        enum PointFlags {
            kCircles_PointFlag            = 0x01,   // draw points as circles (instead of rects)
            kUsePath_PointFlag            = 0x02,   // draw points as stamps of the returned path
            kUseClip_PointFlag            = 0x04,   // apply 'fClipRect' before drawing the points
        };

        uint32_t           fFlags;      // flags that impact the drawing of the points
        SkPoint*           fPoints;     // the center point of each generated point
        int                fNumPoints;  // number of points in fPoints
        SkVector           fSize;       // the size to draw the points
        SkRect             fClipRect;   // clip required to draw the points (if kUseClip is set)
        SkPath             fPath;       // 'stamp' to be used at each point (if kUsePath is set)

        SkPath             fFirst;      // If not empty, contains geometry for first point
        SkPath             fLast;       // If not empty, contains geometry for last point
    };

    /**
     *  Does applying this path effect to 'src' yield a set of points? If so,
     *  optionally return the points in 'results'.
     */
    bool asPoints(PointData* results, const SkPath& src,
                          const SkStrokeRec&, const SkMatrix&,
                          const SkRect* cullR) const;

    /**
     * Filter the input path.
     *
     * The CTM parameter is provided for path effects that can use the information.
     * The output of path effects must always be in the original (input) coordinate system,
     * regardless of whether the path effect uses the CTM or not.
     */
    virtual bool onFilterPath(SkPath*, const SkPath&, SkStrokeRec*, const SkRect*,
                              const SkMatrix& /* ctm */) const = 0;

    /** Path effects *requiring* a valid CTM should override to return true. */
    virtual bool onNeedsCTM() const { return false; }

    virtual bool onAsPoints(PointData*, const SkPath&, const SkStrokeRec&, const SkMatrix&,
                            const SkRect*) const {
        return false;
    }
    virtual DashType onAsADash(DashInfo*) const {
        return kNone_DashType;
    }


    // Compute a conservative bounds for its effect, given the bounds of the path. 'bounds' is
    // both the input and output; if false is returned, fast bounds could not be calculated and
    // 'bounds' is undefined.
    //
    // If 'bounds' is null, performs a dry-run determining if bounds could be computed.
    virtual bool computeFastBounds(SkRect* bounds) const = 0;

private:
    using INHERITED = SkPathEffect;
};

////////////////////////////////////////////////////////////////////////////////////////////////

static inline SkPathEffectBase* as_PEB(SkPathEffect* effect) {
    return static_cast<SkPathEffectBase*>(effect);
}

static inline const SkPathEffectBase* as_PEB(const SkPathEffect* effect) {
    return static_cast<const SkPathEffectBase*>(effect);
}

static inline const SkPathEffectBase* as_PEB(const sk_sp<SkPathEffect>& effect) {
    return static_cast<SkPathEffectBase*>(effect.get());
}

static inline sk_sp<SkPathEffectBase> as_PEB_sp(sk_sp<SkPathEffect> effect) {
    return sk_sp<SkPathEffectBase>(static_cast<SkPathEffectBase*>(effect.release()));
}
}  // namespace pk
