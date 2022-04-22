/*
 * Copyright 2009 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkCubicClipper.h"

namespace pk {
bool SkCubicClipper::ChopMonoAtY(const SkPoint pts[4], SkScalar y, SkScalar* t) {
    SkScalar ycrv[4];
    ycrv[0] = pts[0].fY - y;
    ycrv[1] = pts[1].fY - y;
    ycrv[2] = pts[2].fY - y;
    ycrv[3] = pts[3].fY - y;
    // Check that the endpoints straddle zero.
    SkScalar tNeg, tPos;    // Negative and positive function parameters.
    if (ycrv[0] < 0) {
        if (ycrv[3] < 0)
            return false;
        tNeg = 0;
        tPos = PK_Scalar1;
    } else if (ycrv[0] > 0) {
        if (ycrv[3] > 0)
            return false;
        tNeg = PK_Scalar1;
        tPos = 0;
    } else {
        *t = 0;
        return true;
    }

    const SkScalar tol = PK_Scalar1 / 65536;  // 1 for fixed, 1e-5 for float.
    int iters = 0;
    do {
        SkScalar tMid = (tPos + tNeg) / 2;
        SkScalar y01   = SkScalarInterp(ycrv[0], ycrv[1], tMid);
        SkScalar y12   = SkScalarInterp(ycrv[1], ycrv[2], tMid);
        SkScalar y23   = SkScalarInterp(ycrv[2], ycrv[3], tMid);
        SkScalar y012  = SkScalarInterp(y01,     y12,     tMid);
        SkScalar y123  = SkScalarInterp(y12,     y23,     tMid);
        SkScalar y0123 = SkScalarInterp(y012,    y123,    tMid);
        if (y0123 == 0) {
            *t = tMid;
            return true;
        }
        if (y0123 < 0)  tNeg = tMid;
        else            tPos = tMid;
        ++iters;
    } while (!(PkScalarAbs(tPos - tNeg) <= tol));   // Nan-safe

    *t = (tNeg + tPos) / 2;
    return true;
}

}  // namespace pk