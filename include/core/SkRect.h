/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <algorithm>
#include <utility>

#include "include/core/SkPoint.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTFitsIn.h"

namespace pk {
/** \struct SkRect
    SkRect holds four SkScalar coordinates describing the upper and
    lower bounds of a rectangle. SkRect may be created from outer bounds or
    from position, width, and height. SkRect describes an area; if its right
    is less than or equal to its left, or if its bottom is less than or equal to
    its top, it is considered empty.
*/
struct PK_API SkRect {
    SkScalar fLeft;   //!< smaller x-axis bounds
    SkScalar fTop;    //!< smaller y-axis bounds
    SkScalar fRight;  //!< larger x-axis bounds
    SkScalar fBottom; //!< larger y-axis bounds

    /** Returns constructed SkRect set to (0, 0, 0, 0).
        Many other rectangles are empty; if left is equal to or greater than right,
        or if top is equal to or greater than bottom. Setting all members to zero
        is a convenience, but does not designate a special empty rectangle.

        @return  bounds (0, 0, 0, 0)
    */
    static constexpr SkRect PK_WARN_UNUSED_RESULT MakeEmpty() {
        return SkRect{0, 0, 0, 0};
    }

    /** Returns constructed SkRect set to SkScalar values (0, 0, w, h). Does not
        validate input; w or h may be negative.

        Passing integer values may generate a compiler warning since SkRect cannot
        represent 32-bit integers exactly. Use SkIRect for an exact integer rectangle.

        @param w  SkScalar width of constructed SkRect
        @param h  SkScalar height of constructed SkRect
        @return   bounds (0, 0, w, h)
    */
    static constexpr SkRect PK_WARN_UNUSED_RESULT MakeWH(SkScalar w, SkScalar h) {
        return SkRect{0, 0, w, h};
    }

    /** Returns constructed SkRect set to integer values (0, 0, w, h). Does not validate
        input; w or h may be negative.

        Use to avoid a compiler warning that input may lose precision when stored.
        Use SkIRect for an exact integer rectangle.

        @param w  integer width of constructed SkRect
        @param h  integer height of constructed SkRect
        @return   bounds (0, 0, w, h)
    */
    static SkRect PK_WARN_UNUSED_RESULT MakeIWH(int w, int h) {
        return {0, 0, PkIntToScalar(w), PkIntToScalar(h)};
    }

    /** Returns constructed SkRect set to (l, t, r, b). Does not sort input; SkRect may
        result in fLeft greater than fRight, or fTop greater than fBottom.

        @param l  SkScalar stored in fLeft
        @param t  SkScalar stored in fTop
        @param r  SkScalar stored in fRight
        @param b  SkScalar stored in fBottom
        @return   bounds (l, t, r, b)
    */
    static constexpr SkRect PK_WARN_UNUSED_RESULT MakeLTRB(SkScalar l, SkScalar t, SkScalar r,
                                                           SkScalar b) {
        return SkRect {l, t, r, b};
    }

    /** Returns constructed SkRect set to (x, y, x + w, y + h).
        Does not validate input; w or h may be negative.

        @param x  stored in fLeft
        @param y  stored in fTop
        @param w  added to x and stored in fRight
        @param h  added to y and stored in fBottom
        @return   bounds at (x, y) with width w and height h
    */
    static constexpr SkRect PK_WARN_UNUSED_RESULT MakeXYWH(SkScalar x, SkScalar y, SkScalar w,
                                                           SkScalar h) {
        return SkRect {x, y, x + w, y + h};
    }

    /** Returns true if fLeft is equal to or greater than fRight, or if fTop is equal
        to or greater than fBottom. Call sort() to reverse rectangles with negative
        width() or height().

        @return  true if width() or height() are zero or negative
    */
    bool isEmpty() const {
        // We write it as the NOT of a non-empty rect, so we will return true if any values
        // are NaN.
        return !(fLeft < fRight && fTop < fBottom);
    }

    /** Returns true if fLeft is equal to or less than fRight, or if fTop is equal
        to or less than fBottom. Call sort() to reverse rectangles with negative
        width() or height().

        @return  true if width() or height() are zero or positive
    */
    bool isSorted() const { return fLeft <= fRight && fTop <= fBottom; }

    /** Returns true if all values in the rectangle are finite: PK_ScalarMin or larger,
        and PK_ScalarMax or smaller.

        @return  true if no member is infinite or NaN
    */
    bool isFinite() const {
        float accum = 0;
        accum *= fLeft;
        accum *= fTop;
        accum *= fRight;
        accum *= fBottom;
        // value==value will be true iff value is not NaN
        // TODO: is it faster to say !accum or accum==accum?
        return !SkScalarIsNaN(accum);
    }

    /** Returns left edge of SkRect, if sorted. Call isSorted() to see if SkRect is valid.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fLeft
    */
    SkScalar    x() const { return fLeft; }

    /** Returns top edge of SkRect, if sorted. Call isEmpty() to see if SkRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fTop
    */
    SkScalar    y() const { return fTop; }

    /** Returns left edge of SkRect, if sorted. Call isSorted() to see if SkRect is valid.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fLeft
    */
    SkScalar    left() const { return fLeft; }

    /** Returns top edge of SkRect, if sorted. Call isEmpty() to see if SkRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fTop
    */
    SkScalar    top() const { return fTop; }

    /** Returns right edge of SkRect, if sorted. Call isSorted() to see if SkRect is valid.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fRight
    */
    SkScalar    right() const { return fRight; }

    /** Returns bottom edge of SkRect, if sorted. Call isEmpty() to see if SkRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fBottom
    */
    SkScalar    bottom() const { return fBottom; }

    /** Returns span on the x-axis. This does not check if SkRect is sorted, or if
        result fits in 32-bit float; result may be negative or infinity.

        @return  fRight minus fLeft
    */
    SkScalar    width() const { return fRight - fLeft; }

    /** Returns span on the y-axis. This does not check if SkRect is sorted, or if
        result fits in 32-bit float; result may be negative or infinity.

        @return  fBottom minus fTop
    */
    SkScalar    height() const { return fBottom - fTop; }

    /** Returns average of left edge and right edge. Result does not change if SkRect
        is sorted. Result may overflow to infinity if SkRect is far from the origin.

        @return  midpoint on x-axis
    */
    SkScalar centerX() const {
        // don't use SkScalarHalf(fLeft + fBottom) as that might overflow before the 0.5
        return PkScalarHalf(fLeft) + PkScalarHalf(fRight);
    }

    /** Returns average of top edge and bottom edge. Result does not change if SkRect
        is sorted.

        @return  midpoint on y-axis
    */
    SkScalar centerY() const {
        // don't use SkScalarHalf(fTop + fBottom) as that might overflow before the 0.5
        return PkScalarHalf(fTop) + PkScalarHalf(fBottom);
    }

    /** Returns true if all members in a: fLeft, fTop, fRight, and fBottom; are
        equal to the corresponding members in b.

        a and b are not equal if either contain NaN. a and b are equal if members
        contain zeroes with different signs.

        @param a  SkRect to compare
        @param b  SkRect to compare
        @return   true if members are equal
    */
    friend bool operator==(const SkRect& a, const SkRect& b) {
        return SkScalarsEqual((const SkScalar*)&a, (const SkScalar*)&b, 4);
    }

    /** Returns true if any in a: fLeft, fTop, fRight, and fBottom; does not
        equal the corresponding members in b.

        a and b are not equal if either contain NaN. a and b are equal if members
        contain zeroes with different signs.

        @param a  SkRect to compare
        @param b  SkRect to compare
        @return   true if members are not equal
    */
    friend bool operator!=(const SkRect& a, const SkRect& b) {
        return !SkScalarsEqual((const SkScalar*)&a, (const SkScalar*)&b, 4);
    }

    /** Returns four points in quad that enclose SkRect ordered as: top-left, top-right,
        bottom-right, bottom-left.

        TODO: Consider adding parameter to control whether quad is clockwise or counterclockwise.

        @param quad  storage for corners of SkRect

        example: https://fiddle.skia.org/c/@Rect_toQuad
    */
    void toQuad(SkPoint quad[4]) const;

    /** Sets SkRect to (0, 0, 0, 0).

        Many other rectangles are empty; if left is equal to or greater than right,
        or if top is equal to or greater than bottom. Setting all members to zero
        is a convenience, but does not designate a special empty rectangle.
    */
    void setEmpty() { *this = MakeEmpty(); }

    /** Sets SkRect to (left, top, right, bottom).
        left and right are not sorted; left is not necessarily less than right.
        top and bottom are not sorted; top is not necessarily less than bottom.

        @param left    stored in fLeft
        @param top     stored in fTop
        @param right   stored in fRight
        @param bottom  stored in fBottom
    */
    void setLTRB(SkScalar left, SkScalar top, SkScalar right, SkScalar bottom) {
        fLeft   = left;
        fTop    = top;
        fRight  = right;
        fBottom = bottom;
    }

    /** Sets to bounds of SkPoint array with count entries. If count is zero or smaller,
        or if SkPoint array contains an infinity or NaN, sets to (0, 0, 0, 0).

        Result is either empty or sorted: fLeft is less than or equal to fRight, and
        fTop is less than or equal to fBottom.

        @param pts    SkPoint array
        @param count  entries in array
    */
    void setBounds(const SkPoint pts[], int count) {
        (void)this->setBoundsCheck(pts, count);
    }

    /** Sets to bounds of SkPoint array with count entries. Returns false if count is
        zero or smaller, or if SkPoint array contains an infinity or NaN; in these cases
        sets SkRect to (0, 0, 0, 0).

        Result is either empty or sorted: fLeft is less than or equal to fRight, and
        fTop is less than or equal to fBottom.

        @param pts    SkPoint array
        @param count  entries in array
        @return       true if all SkPoint values are finite

        example: https://fiddle.skia.org/c/@Rect_setBoundsCheck
    */
    bool setBoundsCheck(const SkPoint pts[], int count);

    /** Sets to bounds of SkPoint pts array with count entries. If any SkPoint in pts
        contains infinity or NaN, all SkRect dimensions are set to NaN.

        @param pts    SkPoint array
        @param count  entries in array

        example: https://fiddle.skia.org/c/@Rect_setBoundsNoCheck
    */
    void setBoundsNoCheck(const SkPoint pts[], int count);

    /** Sets bounds to the smallest SkRect enclosing SkPoint p0 and p1. The result is
        sorted and may be empty. Does not check to see if values are finite.

        @param p0  corner to include
        @param p1  corner to include
    */
    void set(const SkPoint& p0, const SkPoint& p1) {
        fLeft =   std::min(p0.fX, p1.fX);
        fRight =  std::max(p0.fX, p1.fX);
        fTop =    std::min(p0.fY, p1.fY);
        fBottom = std::max(p0.fY, p1.fY);
    }

    /** Sets SkRect to (x, y, x + width, y + height).
        Does not validate input; width or height may be negative.

        @param x       stored in fLeft
        @param y       stored in fTop
        @param width   added to x and stored in fRight
        @param height  added to y and stored in fBottom
    */
    void setXYWH(SkScalar x, SkScalar y, SkScalar width, SkScalar height) {
        fLeft = x;
        fTop = y;
        fRight = x + width;
        fBottom = y + height;
    }

    /** Sets SkRect to (0, 0, width, height). Does not validate input;
        width or height may be negative.

        @param width   stored in fRight
        @param height  stored in fBottom
    */
    void setWH(SkScalar width, SkScalar height) {
        fLeft = 0;
        fTop = 0;
        fRight = width;
        fBottom = height;
    }
    void setIWH(int32_t width, int32_t height) {
        this->setWH(PkIntToScalar(width), PkIntToScalar(height));
    }

    /** Returns SkRect offset by (dx, dy).

        If dx is negative, SkRect returned is moved to the left.
        If dx is positive, SkRect returned is moved to the right.
        If dy is negative, SkRect returned is moved upward.
        If dy is positive, SkRect returned is moved downward.

        @param dx  added to fLeft and fRight
        @param dy  added to fTop and fBottom
        @return    SkRect offset on axes, with original width and height
    */
    constexpr SkRect makeOffset(SkScalar dx, SkScalar dy) const {
        return MakeLTRB(fLeft + dx, fTop + dy, fRight + dx, fBottom + dy);
    }

    /** Returns SkRect offset by v.

        @param v  added to rect
        @return    SkRect offset on axes, with original width and height
    */
    constexpr SkRect makeOffset(SkVector v) const { return this->makeOffset(v.x(), v.y()); }

    /** Returns SkRect, inset by (dx, dy).

        If dx is negative, SkRect returned is wider.
        If dx is positive, SkRect returned is narrower.
        If dy is negative, SkRect returned is taller.
        If dy is positive, SkRect returned is shorter.

        @param dx  added to fLeft and subtracted from fRight
        @param dy  added to fTop and subtracted from fBottom
        @return    SkRect inset symmetrically left and right, top and bottom
    */
    SkRect makeInset(SkScalar dx, SkScalar dy) const {
        return MakeLTRB(fLeft + dx, fTop + dy, fRight - dx, fBottom - dy);
    }

    /** Returns SkRect, outset by (dx, dy).

        If dx is negative, SkRect returned is narrower.
        If dx is positive, SkRect returned is wider.
        If dy is negative, SkRect returned is shorter.
        If dy is positive, SkRect returned is taller.

        @param dx  subtracted to fLeft and added from fRight
        @param dy  subtracted to fTop and added from fBottom
        @return    SkRect outset symmetrically left and right, top and bottom
    */
    SkRect makeOutset(SkScalar dx, SkScalar dy) const {
        return MakeLTRB(fLeft - dx, fTop - dy, fRight + dx, fBottom + dy);
    }

    /** Offsets SkRect by adding dx to fLeft, fRight; and by adding dy to fTop, fBottom.

        If dx is negative, moves SkRect to the left.
        If dx is positive, moves SkRect to the right.
        If dy is negative, moves SkRect upward.
        If dy is positive, moves SkRect downward.

        @param dx  offset added to fLeft and fRight
        @param dy  offset added to fTop and fBottom
    */
    void offset(SkScalar dx, SkScalar dy) {
        fLeft   += dx;
        fTop    += dy;
        fRight  += dx;
        fBottom += dy;
    }

    /** Offsets SkRect by adding delta.fX to fLeft, fRight; and by adding delta.fY to
        fTop, fBottom.

        If delta.fX is negative, moves SkRect to the left.
        If delta.fX is positive, moves SkRect to the right.
        If delta.fY is negative, moves SkRect upward.
        If delta.fY is positive, moves SkRect downward.

        @param delta  added to SkRect
    */
    void offset(const SkPoint& delta) {
        this->offset(delta.fX, delta.fY);
    }

    /** Offsets SkRect so that fLeft equals newX, and fTop equals newY. width and height
        are unchanged.

        @param newX  stored in fLeft, preserving width()
        @param newY  stored in fTop, preserving height()
    */
    void offsetTo(SkScalar newX, SkScalar newY) {
        fRight += newX - fLeft;
        fBottom += newY - fTop;
        fLeft = newX;
        fTop = newY;
    }

    /** Insets SkRect by (dx, dy).

        If dx is positive, makes SkRect narrower.
        If dx is negative, makes SkRect wider.
        If dy is positive, makes SkRect shorter.
        If dy is negative, makes SkRect taller.

        @param dx  added to fLeft and subtracted from fRight
        @param dy  added to fTop and subtracted from fBottom
    */
    void inset(SkScalar dx, SkScalar dy)  {
        fLeft   += dx;
        fTop    += dy;
        fRight  -= dx;
        fBottom -= dy;
    }

    /** Outsets SkRect by (dx, dy).

        If dx is positive, makes SkRect wider.
        If dx is negative, makes SkRect narrower.
        If dy is positive, makes SkRect taller.
        If dy is negative, makes SkRect shorter.

        @param dx  subtracted to fLeft and added from fRight
        @param dy  subtracted to fTop and added from fBottom
    */
    void outset(SkScalar dx, SkScalar dy)  { this->inset(-dx, -dy); }

    /** Returns true if SkRect intersects r, and sets SkRect to intersection.
        Returns false if SkRect does not intersect r, and leaves SkRect unchanged.

        Returns false if either r or SkRect is empty, leaving SkRect unchanged.

        @param r  limit of result
        @return   true if r and SkRect have area in common

        example: https://fiddle.skia.org/c/@Rect_intersect
    */
    bool intersect(const SkRect& r);

    /** Returns true if a intersects b, and sets SkRect to intersection.
        Returns false if a does not intersect b, and leaves SkRect unchanged.

        Returns false if either a or b is empty, leaving SkRect unchanged.

        @param a  SkRect to intersect
        @param b  SkRect to intersect
        @return   true if a and b have area in common
    */
    bool PK_WARN_UNUSED_RESULT intersect(const SkRect& a, const SkRect& b);


private:
    static bool Intersects(SkScalar al, SkScalar at, SkScalar ar, SkScalar ab,
                           SkScalar bl, SkScalar bt, SkScalar br, SkScalar bb) {
        SkScalar L = std::max(al, bl);
        SkScalar R = std::min(ar, br);
        SkScalar T = std::max(at, bt);
        SkScalar B = std::min(ab, bb);
        return L < R && T < B;
    }

public:

    /** Returns true if SkRect intersects r.
     Returns false if either r or SkRect is empty, or do not intersect.

     @param r  SkRect to intersect
     @return   true if r and SkRect have area in common
     */
    bool intersects(const SkRect& r) const {
        return Intersects(fLeft, fTop, fRight, fBottom,
                          r.fLeft, r.fTop, r.fRight, r.fBottom);
    }

    /** Returns true if a intersects b.
        Returns false if either a or b is empty, or do not intersect.

        @param a  SkRect to intersect
        @param b  SkRect to intersect
        @return   true if a and b have area in common
    */
    static bool Intersects(const SkRect& a, const SkRect& b) {
        return Intersects(a.fLeft, a.fTop, a.fRight, a.fBottom,
                          b.fLeft, b.fTop, b.fRight, b.fBottom);
    }

    /** Sets SkRect to the union of itself and r.

        Has no effect if r is empty. Otherwise, if SkRect is empty, sets
        SkRect to r.

        @param r  expansion SkRect

        example: https://fiddle.skia.org/c/@Rect_join_2
    */
    void join(const SkRect& r);

    /** Sets SkRect to the union of itself and r.

        Asserts if r is empty and PK_DEBUG is defined.
        If SkRect is empty, sets SkRect to r.

        May produce incorrect results if r is empty.

        @param r  expansion SkRect
    */
    void joinNonEmptyArg(const SkRect& r) {
        // if we are empty, just assign
        if (fLeft >= fRight || fTop >= fBottom) {
            *this = r;
        } else {
            this->joinPossiblyEmptyRect(r);
        }
    }

    /** Sets SkRect to the union of itself and the construction.

        May produce incorrect results if SkRect or r is empty.

        @param r  expansion SkRect
    */
    void joinPossiblyEmptyRect(const SkRect& r) {
        fLeft   = std::min(fLeft, r.left());
        fTop    = std::min(fTop, r.top());
        fRight  = std::max(fRight, r.right());
        fBottom = std::max(fBottom, r.bottom());
    }

    /** Returns true if: fLeft <= x < fRight && fTop <= y < fBottom.
        Returns false if SkRect is empty.

        @param x  test SkPoint x-coordinate
        @param y  test SkPoint y-coordinate
        @return   true if (x, y) is inside SkRect
    */
    bool contains(SkScalar x, SkScalar y) const {
        return x >= fLeft && x < fRight && y >= fTop && y < fBottom;
    }

    /** Returns true if SkRect contains r.
        Returns false if SkRect is empty or r is empty.

        SkRect contains r when SkRect area completely includes r area.

        @param r  SkRect contained
        @return   true if all sides of SkRect are outside r
    */
    bool contains(const SkRect& r) const {
        // todo: can we eliminate the this->isEmpty check?
        return  !r.isEmpty() && !this->isEmpty() &&
                fLeft <= r.fLeft && fTop <= r.fTop &&
                fRight >= r.fRight && fBottom >= r.fBottom;
    }

    /** Sets SkRect by discarding the fractional portion of fLeft and fTop; and rounding
        up fRight and fBottom, using
        (SkScalarFloorToInt(fLeft), SkScalarFloorToInt(fTop),
         SkScalarCeilToInt(fRight), SkScalarCeilToInt(fBottom)).

        @param dst  storage for SkRect
    */
    void roundOut(SkRect* dst) const {
        dst->setLTRB(PkScalarFloorToScalar(fLeft), PkScalarFloorToScalar(fTop),
                     PkScalarCeilToScalar(fRight), PkScalarCeilToScalar(fBottom));
    }

    /** Swaps fLeft and fRight if fLeft is greater than fRight; and swaps
        fTop and fBottom if fTop is greater than fBottom. Result may be empty;
        and width() and height() will be zero or positive.
    */
    void sort() {
        using std::swap;
        if (fLeft > fRight) {
            swap(fLeft, fRight);
        }

        if (fTop > fBottom) {
            swap(fTop, fBottom);
        }
    }

    /** Returns SkRect with fLeft and fRight swapped if fLeft is greater than fRight; and
        with fTop and fBottom swapped if fTop is greater than fBottom. Result may be empty;
        and width() and height() will be zero or positive.

        @return  sorted SkRect
    */
    SkRect makeSorted() const {
        return MakeLTRB(std::min(fLeft, fRight), std::min(fTop, fBottom),
                        std::max(fLeft, fRight), std::max(fTop, fBottom));
    }

};

}  // namespace pk
