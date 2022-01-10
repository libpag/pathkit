/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathTypes.h"
#include "include/private/SkTDArray.h"

namespace pk {
class PK_API SkPathBuilder {
public:
    SkPathBuilder();
    SkPathBuilder(const SkPathBuilder&) = default;
    ~SkPathBuilder();

    SkPathBuilder& operator=(const SkPath&);
    SkPathBuilder& operator=(const SkPathBuilder&) = default;

    SkPathFillType fillType() const { return fFillType; }

    SkPath snapshot() const;  // the builder is unchanged after returning this path
    SkPath detach();    // the builder is reset to empty after returning this path

    SkPathBuilder& setFillType(SkPathFillType ft) { fFillType = ft; return *this; }

    SkPathBuilder& reset();

    SkPathBuilder& moveTo(SkPoint pt);
    SkPathBuilder& moveTo(SkScalar x, SkScalar y) { return this->moveTo(SkPoint::Make(x, y)); }

    SkPathBuilder& lineTo(SkPoint pt);
    SkPathBuilder& lineTo(SkScalar x, SkScalar y) { return this->lineTo(SkPoint::Make(x, y)); }

    SkPathBuilder& quadTo(SkPoint pt1, SkPoint pt2);
    SkPathBuilder& quadTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2) {
        return this->quadTo(SkPoint::Make(x1, y1), SkPoint::Make(x2, y2));
    }
    SkPathBuilder& quadTo(const SkPoint pts[2]) { return this->quadTo(pts[0], pts[1]); }

    SkPathBuilder& conicTo(SkPoint pt1, SkPoint pt2, SkScalar w);
    SkPathBuilder& conicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2, SkScalar w) {
        return this->conicTo(SkPoint::Make(x1, y1), SkPoint::Make(x2, y2), w);
    }
    SkPathBuilder& conicTo(const SkPoint pts[2], SkScalar w) {
        return this->conicTo(pts[0], pts[1], w);
    }

    SkPathBuilder& cubicTo(SkPoint pt1, SkPoint pt2, SkPoint pt3);
    SkPathBuilder& cubicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2, SkScalar x3, SkScalar y3) {
        return this->cubicTo(SkPoint::Make(x1, y1), SkPoint::Make(x2, y2), SkPoint::Make(x3, y3));
    }
    SkPathBuilder& cubicTo(const SkPoint pts[3]) {
        return this->cubicTo(pts[0], pts[1], pts[2]);
    }

    SkPathBuilder& close();

    // Append a series of lineTo(...)
    SkPathBuilder& polylineTo(const SkPoint pts[], int count);
    SkPathBuilder& polylineTo(const std::initializer_list<SkPoint>& list) {
        return this->polylineTo(list.begin(), SkToInt(list.size()));
    }

    // Add a new contour

    SkPathBuilder& addRect(const SkRect&, SkPathDirection, unsigned startIndex);
    SkPathBuilder& addOval(const SkRect&, SkPathDirection, unsigned startIndex);
    SkPathBuilder& addRRect(const SkRRect&, SkPathDirection, unsigned startIndex);

    SkPathBuilder& addRect(const SkRect& rect, SkPathDirection dir = SkPathDirection::kCW) {
        return this->addRect(rect, dir, 0);
    }
    SkPathBuilder& addOval(const SkRect& rect, SkPathDirection dir = SkPathDirection::kCW) {
        // legacy start index: 1
        return this->addOval(rect, dir, 1);
    }
    SkPathBuilder& addRRect(const SkRRect& rrect, SkPathDirection dir = SkPathDirection::kCW) {
        // legacy start indices: 6 (CW) and 7 (CCW)
        return this->addRRect(rrect, dir, dir == SkPathDirection::kCW ? 6 : 7);
    }

    SkPathBuilder& addCircle(SkScalar center_x, SkScalar center_y, SkScalar radius,
                             SkPathDirection dir = SkPathDirection::kCW);

    SkPathBuilder& addPolygon(const SkPoint pts[], int count, bool isClosed);
    SkPathBuilder& addPolygon(const std::initializer_list<SkPoint>& list, bool isClosed) {
        return this->addPolygon(list.begin(), SkToInt(list.size()), isClosed);
    }

    SkPathBuilder& addPath(const SkPath&);

    // Performance hint, to reserve extra storage for subsequent calls to lineTo, quadTo, etc.

    void incReserve(int extraPtCount, int extraVerbCount);
    void incReserve(int extraPtCount) {
        this->incReserve(extraPtCount, extraPtCount);
    }

    SkPathBuilder& offset(SkScalar dx, SkScalar dy);

    SkPathBuilder& toggleInverseFillType() {
        fFillType = (SkPathFillType)((unsigned)fFillType ^ 2);
        return *this;
    }

private:
    SkTDArray<SkPoint>  fPts;
    SkTDArray<uint8_t>  fVerbs;
    SkTDArray<SkScalar> fConicWeights;

    SkPathFillType      fFillType;

    unsigned    fSegmentMask;
    SkPoint     fLastMovePoint;
    int         fLastMoveIndex; // only needed until SkPath is immutable
    bool        fNeedsMoveVerb;

    enum IsA {
        kIsA_JustMoves,     // we only have 0 or more moves
        kIsA_MoreThanMoves, // we have verbs other than just move
        kIsA_Oval,          // we are 0 or more moves followed by an oval
        kIsA_RRect,         // we are 0 or more moves followed by a rrect
    };
    IsA fIsA      = kIsA_JustMoves;
    int fIsAStart = -1;     // tracks direction iff fIsA is not unknown
    bool fIsACCW  = false;  // tracks direction iff fIsA is not unknown

    // for testing
    SkPathConvexity fOverrideConvexity = SkPathConvexity::kUnknown;

    int countVerbs() const { return fVerbs.count(); }

    // called right before we add a (non-move) verb
    void ensureMove() {
        fIsA = kIsA_MoreThanMoves;
        if (fNeedsMoveVerb) {
            this->moveTo(fLastMovePoint);
        }
    }

    SkPath make(sk_sp<SkPathRef>) const;

    SkPathBuilder& privateReverseAddPath(const SkPath&);

    // For testing
    void privateSetConvexity(SkPathConvexity c) { fOverrideConvexity = c; }

    friend class SkPathPriv;
};
}  // namespace pk
