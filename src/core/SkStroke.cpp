/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkStroke.h"

#include <utility>

#include "include/private/SkTo.h"
#include "src/core/SkPaintDefaults.h"
#include "src/core/SkPathPriv.h"
#include "include/core/SkPathStroker.h"

namespace pk {

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

SkStroke::SkStroke() {
    fWidth      = PK_Scalar1;
    fMiterLimit = PkPaintDefaults_MiterLimit;
    fResScale   = 1;
    fCap        = SkPaint::kDefault_Cap;
    fJoin       = SkPaint::kDefault_Join;
    fDoFill     = false;
}

void SkStroke::setWidth(SkScalar width) {
    fWidth = width;
}

void SkStroke::setMiterLimit(SkScalar miterLimit) {
    fMiterLimit = miterLimit;
}

void SkStroke::setCap(SkPaint::Cap cap) {
    fCap = SkToU8(cap);
}

void SkStroke::setJoin(SkPaint::Join join) {
    fJoin = SkToU8(join);
}

///////////////////////////////////////////////////////////////////////////////

// If src==dst, then we use a tmp path to record the stroke, and then swap
// its contents with src when we're done.
class AutoTmpPath {
public:
    AutoTmpPath(const SkPath& src, SkPath** dst) : fSrc(src) {
        if (&src == *dst) {
            *dst = &fTmpDst;
            fSwapWithSrc = true;
        } else {
            (*dst)->reset();
            fSwapWithSrc = false;
        }
    }

    ~AutoTmpPath() {
        if (fSwapWithSrc) {
            fTmpDst.swap(*const_cast<SkPath*>(&fSrc));
        }
    }

private:
    SkPath          fTmpDst;
    const SkPath&   fSrc;
    bool            fSwapWithSrc;
};

void SkStroke::strokePath(const SkPath& src, SkPath* dst) const {

    SkScalar radius = PkScalarHalf(fWidth);

    AutoTmpPath tmp(src, &dst);

    if (radius <= 0) {
        return;
    }

    // If src is really a rect, call our specialty strokeRect() method
    {
        SkRect rect;
        bool isClosed = false;
        SkPathDirection dir;
        if (src.isRect(&rect, &isClosed, &dir) && isClosed) {
            this->strokeRect(rect, dst, dir);
            // our answer should preserve the inverseness of the src
            if (src.isInverseFillType()) {
                dst->toggleInverseFillType();
            }
            return;
        }
    }

    // We can always ignore centers for stroke and fill convex line-only paths
    // TODO: remove the line-only restriction
    bool ignoreCenter = fDoFill && (src.getSegmentMasks() == SkPath::kLine_SegmentMask) &&
                        src.isLastContourClosed() && src.isConvex();

    SkStrokeParams params(fMiterLimit, this->getCap(), this->getJoin());
    SkPathStroker stroker(src, radius, fResScale, ignoreCenter);
    SkPath::Iter    iter(src, false);
    SkPath::Verb    lastSegment = SkPath::kMove_Verb;

    for (;;) {
        SkPoint  pts[4];
        switch (iter.next(pts)) {
            case SkPath::kMove_Verb:
                stroker.moveTo(pts[0]);
                break;
            case SkPath::kLine_Verb:
                stroker.lineTo(pts[1], params, &iter);
                lastSegment = SkPath::kLine_Verb;
                break;
            case SkPath::kQuad_Verb:
                stroker.quadTo(pts[1], pts[2], params);
                lastSegment = SkPath::kQuad_Verb;
                break;
            case SkPath::kConic_Verb: {
                stroker.conicTo(pts[1], pts[2], iter.conicWeight(), params);
                lastSegment = SkPath::kConic_Verb;
                break;
            } break;
            case SkPath::kCubic_Verb:
                stroker.cubicTo(pts[1], pts[2], pts[3], params);
                lastSegment = SkPath::kCubic_Verb;
                break;
            case SkPath::kClose_Verb:
                if (SkPaint::kButt_Cap != this->getCap()) {
                    /* If the stroke consists of a moveTo followed by a close, treat it
                       as if it were followed by a zero-length line. Lines without length
                       can have square and round end caps. */
                    if (stroker.hasOnlyMoveTo()) {
                        stroker.lineTo(stroker.moveToPt(), params);
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
                stroker.close(lastSegment == SkPath::kLine_Verb, params);
                break;
            case SkPath::kDone_Verb:
                goto DONE;
        }
    }
DONE:
    stroker.done(dst, lastSegment == SkPath::kLine_Verb, params);

    if (fDoFill && !ignoreCenter) {
        if (SkPathPriv::ComputeFirstDirection(src) == SkPathFirstDirection::kCCW) {
            dst->reverseAddPath(src);
        } else {
            dst->addPath(src);
        }
    } else {
        //  Seems like we can assume that a 2-point src would always result in
        //  a convex stroke, but testing has proved otherwise.
        //  TODO: fix the stroker to make this assumption true (without making
        //  it slower that the work that will be done in computeConvexity())
#if 0
        // this test results in a non-convex stroke :(
        static void test(SkCanvas* canvas) {
            SkPoint pts[] = { 146.333328,  192.333328, 300.333344, 293.333344 };
            SkPaint paint;
            paint.setStrokeWidth(7);
            paint.setStrokeCap(SkPaint::kRound_Cap);
            canvas->drawLine(pts[0].fX, pts[0].fY, pts[1].fX, pts[1].fY, paint);
        }
#endif
#if 0
        if (2 == src.countPoints()) {
            dst->setIsConvex(true);
        }
#endif
    }

    // our answer should preserve the inverseness of the src
    if (src.isInverseFillType()) {
        dst->toggleInverseFillType();
    }
}

static SkPathDirection reverse_direction(SkPathDirection dir) {
    static const SkPathDirection gOpposite[] = { SkPathDirection::kCCW, SkPathDirection::kCW };
    return gOpposite[(int)dir];
}

static void addBevel(SkPath* path, const SkRect& r, const SkRect& outer, SkPathDirection dir) {
    SkPoint pts[8];

    if (SkPathDirection::kCW == dir) {
        pts[0].set(r.fLeft, outer.fTop);
        pts[1].set(r.fRight, outer.fTop);
        pts[2].set(outer.fRight, r.fTop);
        pts[3].set(outer.fRight, r.fBottom);
        pts[4].set(r.fRight, outer.fBottom);
        pts[5].set(r.fLeft, outer.fBottom);
        pts[6].set(outer.fLeft, r.fBottom);
        pts[7].set(outer.fLeft, r.fTop);
    } else {
        pts[7].set(r.fLeft, outer.fTop);
        pts[6].set(r.fRight, outer.fTop);
        pts[5].set(outer.fRight, r.fTop);
        pts[4].set(outer.fRight, r.fBottom);
        pts[3].set(r.fRight, outer.fBottom);
        pts[2].set(r.fLeft, outer.fBottom);
        pts[1].set(outer.fLeft, r.fBottom);
        pts[0].set(outer.fLeft, r.fTop);
    }
    path->addPoly(pts, 8, true);
}

void SkStroke::strokeRect(const SkRect& origRect, SkPath* dst,
                          SkPathDirection dir) const {
    dst->reset();

    SkScalar radius = PkScalarHalf(fWidth);
    if (radius <= 0) {
        return;
    }

    SkScalar rw = origRect.width();
    SkScalar rh = origRect.height();
    if ((rw < 0) ^ (rh < 0)) {
        dir = reverse_direction(dir);
    }
    SkRect rect(origRect);
    rect.sort();
    // reassign these, now that we know they'll be >= 0
    rw = rect.width();
    rh = rect.height();

    SkRect r(rect);
    r.outset(radius, radius);

    SkPaint::Join join = (SkPaint::Join)fJoin;
    if (SkPaint::kMiter_Join == join && fMiterLimit < PK_ScalarSqrt2) {
        join = SkPaint::kBevel_Join;
    }

    switch (join) {
        case SkPaint::kMiter_Join:
            dst->addRect(r, dir);
            break;
        case SkPaint::kBevel_Join:
            addBevel(dst, rect, r, dir);
            break;
        case SkPaint::kRound_Join:
            dst->addRoundRect(r, radius, radius, dir);
            break;
        default:
            break;
    }

    if (fWidth < std::min(rw, rh) && !fDoFill) {
        r = rect;
        r.inset(radius, radius);
        dst->addRect(r, reverse_direction(dir));
    }
}
}  // namespace pk