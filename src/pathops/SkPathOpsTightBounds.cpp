/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkPathPriv.h"
#include "src/pathops/SkOpEdgeBuilder.h"
#include "src/pathops/SkPathOpsCommon.h"

namespace pk {
bool TightBounds(const SkPath& path, SkRect* result) {
    SkRect moveBounds = { PK_ScalarMax, PK_ScalarMax, PK_ScalarMin, PK_ScalarMin };
    bool wellBehaved = true;
    for (auto iter : SkPathPriv::Iterate(path)) {
        auto verb = std::get<0>(iter);
        auto pts = std::get<1>(iter);
        switch (verb) {
            case SkPathVerb::kMove:
                moveBounds.fLeft = std::min(moveBounds.fLeft, pts[0].fX);
                moveBounds.fTop = std::min(moveBounds.fTop, pts[0].fY);
                moveBounds.fRight = std::max(moveBounds.fRight, pts[0].fX);
                moveBounds.fBottom = std::max(moveBounds.fBottom, pts[0].fY);
                break;
            case SkPathVerb::kQuad:
            case SkPathVerb::kConic:
                if (!wellBehaved) {
                    break;
                }
                wellBehaved &= between(pts[0].fX, pts[1].fX, pts[2].fX);
                wellBehaved &= between(pts[0].fY, pts[1].fY, pts[2].fY);
                break;
            case SkPathVerb::kCubic:
                if (!wellBehaved) {
                    break;
                }
                wellBehaved &= between(pts[0].fX, pts[1].fX, pts[3].fX);
                wellBehaved &= between(pts[0].fY, pts[1].fY, pts[3].fY);
                wellBehaved &= between(pts[0].fX, pts[2].fX, pts[3].fX);
                wellBehaved &= between(pts[0].fY, pts[2].fY, pts[3].fY);
                break;
            default:
                break;
        }
    }
    if (wellBehaved) {
        *result = path.getBounds();
        return true;
    }
    SkSTArenaAlloc<4096> allocator;  // FIXME: constant-ize, tune
    SkOpContour contour;
    SkOpContourHead* contourList = static_cast<SkOpContourHead*>(&contour);
    SkOpGlobalState globalState(contourList, &allocator  PkDEBUGPARAMS(false)
            PkDEBUGPARAMS(nullptr));
    // turn path into list of segments
    SkOpEdgeBuilder builder(path, contourList, &globalState);
    if (!builder.finish()) {
        return false;
    }
    if (!SortContourList(&contourList, false, false)) {
        *result = moveBounds;
        return true;
    }
    SkOpContour* current = contourList;
    SkPathOpsBounds bounds = current->bounds();
    while ((current = current->next())) {
        bounds.add(current->bounds());
    }
    *result = bounds;
    if (!moveBounds.isEmpty()) {
        result->join(moveBounds);
    }
    return true;
}
}  // namespace pk