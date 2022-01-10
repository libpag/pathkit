/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkRect.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkNx.h"
#include "src/core/SkRectPriv.h"

namespace pk {
void SkRect::toQuad(SkPoint quad[4]) const {
    quad[0].set(fLeft, fTop);
    quad[1].set(fRight, fTop);
    quad[2].set(fRight, fBottom);
    quad[3].set(fLeft, fBottom);
}

bool SkRect::setBoundsCheck(const SkPoint pts[], int count) {
    if (count <= 0) {
        this->setEmpty();
        return true;
    }

    Sk4s min, max;
    if (count & 1) {
        min = max = Sk4s(pts->fX, pts->fY, pts->fX, pts->fY);
        pts += 1;
        count -= 1;
    } else {
        min = max = Sk4s::Load(pts);
        pts += 2;
        count -= 2;
    }

    Sk4s accum = min * 0;
    while (count) {
        Sk4s xy = Sk4s::Load(pts);
        accum = accum * xy;
        min = Sk4s::Min(min, xy);
        max = Sk4s::Max(max, xy);
        pts += 2;
        count -= 2;
    }

    bool all_finite = (accum * 0 == 0).allTrue();
    if (all_finite) {
        this->setLTRB(std::min(min[0], min[2]),
                      std::min(min[1], min[3]),
                      std::max(max[0], max[2]),
                      std::max(max[1], max[3]));
    } else {
        this->setEmpty();
    }
    return all_finite;
}

void SkRect::setBoundsNoCheck(const SkPoint pts[], int count) {
    if (!this->setBoundsCheck(pts, count)) {
        this->setLTRB(PK_ScalarNaN, PK_ScalarNaN, PK_ScalarNaN, PK_ScalarNaN);
    }
}

#define CHECK_INTERSECT(al, at, ar, ab, bl, bt, br, bb) \
    SkScalar L = std::max(al, bl);                      \
    SkScalar R = std::min(ar, br);                      \
    SkScalar T = std::max(at, bt);                      \
    SkScalar B = std::min(ab, bb);                      \
    do {                                                \
        if (!(L < R && T < B)) return false;            \
    } while (0)
// do the !(opposite) check so we return false if either arg is NaN

bool SkRect::intersect(const SkRect& r) {
    CHECK_INTERSECT(r.fLeft, r.fTop, r.fRight, r.fBottom, fLeft, fTop, fRight, fBottom);
    this->setLTRB(L, T, R, B);
    return true;
}

bool SkRect::intersect(const SkRect& a, const SkRect& b) {
    CHECK_INTERSECT(a.fLeft, a.fTop, a.fRight, a.fBottom, b.fLeft, b.fTop, b.fRight, b.fBottom);
    this->setLTRB(L, T, R, B);
    return true;
}

void SkRect::join(const SkRect& r) {
    if (r.isEmpty()) {
        return;
    }

    if (this->isEmpty()) {
        *this = r;
    } else {
        fLeft = std::min(fLeft, r.fLeft);
        fTop = std::min(fTop, r.fTop);
        fRight = std::max(fRight, r.fRight);
        fBottom = std::max(fBottom, r.fBottom);
    }
}
}  // namespace pk