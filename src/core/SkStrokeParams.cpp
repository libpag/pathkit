/*
 * Copyright 2025 The PathKit Authors
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkStrokeParams.h"

#include "include/core/SkPath.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathStroker.h"

namespace pk {

bool StrokePathWithMultiParams(const SkPath& src,
                               SkPath* dst,
                               SkScalar width,
                               const std::vector<SkStrokeParams>& params,
                               SkScalar resScale) {
    if (params.empty()) {
        return false;
    }

    SkScalar radius = PkScalarHalf(width);
    AutoTmpPath tmp(src, &dst);

    if (radius <= 0) {
        return false;
    }

    // We can always ignore centers for stroke and fill convex line-only paths
    // TODO: remove the line-only restriction
    bool ignoreCenter = (src.getSegmentMasks() == SkPath::kLine_SegmentMask) &&
                        src.isLastContourClosed() && src.isConvex();

    SkPathStroker stroker(src, radius, resScale, ignoreCenter);
    SkPath::Iter iter(src, false);
    SkPath::Verb lastSegment = SkPath::kMove_Verb;

    // Track segment index for cycling through params
    size_t segmentIndex = 0;
    const size_t paramsSize = params.size();
    
    // Helper lambda to get current params
    auto getCurrentParams = [&]() -> const SkStrokeParams& {
        return params[segmentIndex % paramsSize];
    };

    for (;;) {
        SkPoint pts[4];
        SkPath::Verb verb = iter.next(pts);

        switch (verb) {
            case SkPath::kMove_Verb:
                stroker.moveTo(pts[0]);
                break;
            case SkPath::kLine_Verb:
                stroker.lineTo(pts[1], getCurrentParams(), &iter);
                lastSegment = SkPath::kLine_Verb;
                segmentIndex++;
                break;
            case SkPath::kQuad_Verb:
                stroker.quadTo(pts[1], pts[2], getCurrentParams());
                lastSegment = SkPath::kQuad_Verb;
                segmentIndex++;
                break;
            case SkPath::kConic_Verb:
                stroker.conicTo(pts[1], pts[2], iter.conicWeight(), getCurrentParams());
                lastSegment = SkPath::kConic_Verb;
                segmentIndex++;
                break;
            case SkPath::kCubic_Verb:
                stroker.cubicTo(pts[1], pts[2], pts[3], getCurrentParams());
                lastSegment = SkPath::kCubic_Verb;
                segmentIndex++;
                break;
            case SkPath::kClose_Verb: {
                // Use current params for close
                const SkStrokeParams& currentParams = getCurrentParams();
                if (SkPaint::kButt_Cap != currentParams.cap) {
                    /* If the stroke consists of a moveTo followed by a close, treat it
                       as if it were followed by a zero-length line. Lines without length
                       can have square and round end caps. */
                    if (stroker.hasOnlyMoveTo()) {
                        stroker.lineTo(stroker.moveToPt(), currentParams);
                        goto ZERO_LENGTH;
                    }
                    /* If the stroke consists of a moveTo followed by one or more zero-length
                       verbs, then followed by a close, treat is as if it were followed by a
                       zero-length line. Lines without length can have square & round end caps. */
                    if (stroker.isCurrentContourEmpty()) {
                    ZERO_LENGTH:
                        lastSegment = SkPath::kLine_Verb;
                        break;
                    }
                }
                stroker.close(lastSegment == SkPath::kLine_Verb, currentParams);
                segmentIndex++;
                break;
            }
            case SkPath::kDone_Verb:
                goto DONE;
        }
    }
DONE:
    stroker.done(dst, lastSegment == SkPath::kLine_Verb, getCurrentParams());

    // Preserve the inverseness of the src
    if (src.isInverseFillType()) {
        dst->toggleInverseFillType();
    }

    return true;
}

}  // namespace pk
