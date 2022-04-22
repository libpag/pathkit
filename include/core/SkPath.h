/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkMatrix.h"
#include "include/core/SkPathTypes.h"
#include "include/private/SkPathRef.h"
#include "include/private/SkTo.h"

#include <initializer_list>
#include <vector>

namespace pk {
class SkAutoPathBoundsUpdate;
class SkData;
class SkRRect;

/** \class SkPath
    SkPath contain geometry. SkPath may be empty, or contain one or more verbs that
    outline a figure. SkPath always starts with a move verb to a Cartesian coordinate,
    and may be followed by additional verbs that add lines or curves.
    Adding a close verb makes the geometry into a continuous loop, a closed contour.
    SkPath may contain any number of contours, each beginning with a move verb.

    SkPath contours may contain only a move verb, or may also contain lines,
    quadratic beziers, conics, and cubic beziers. SkPath contours may be open or
    closed.

    When used to draw a filled area, SkPath describes whether the fill is inside or
    outside the geometry. SkPath also describes the winding rule used to fill
    overlapping contours.

    Internally, SkPath lazily computes metrics likes bounds and convexity. Call
    SkPath::updateBoundsCache to make SkPath thread safe.
*/
class PK_API SkPath {
public:
    /** Constructs an empty SkPath. By default, SkPath has no verbs, no SkPoint, and no weights.
        FillType is set to kWinding.

        @return  empty SkPath

        example: https://fiddle.skia.org/c/@Path_empty_constructor
    */
    SkPath();

    /** Constructs a copy of an existing path.
        Copy constructor makes two paths identical by value. Internally, path and
        the returned result share pointer values. The underlying verb array, SkPoint array
        and weights are copied when modified.

        Creating a SkPath copy is very efficient and never allocates memory.
        SkPath are always copied by value from the interface; the underlying shared
        pointers are not exposed.

        @param path  SkPath to copy by value
        @return      copy of SkPath

        example: https://fiddle.skia.org/c/@Path_copy_const_SkPath
    */
    SkPath(const SkPath& path);

    /** Constructs a copy of an existing path.
        SkPath assignment makes two paths identical by value. Internally, assignment
        shares pointer values. The underlying verb array, SkPoint array and weights
        are copied when modified.

        Copying SkPath by assignment is very efficient and never allocates memory.
        SkPath are always copied by value from the interface; the underlying shared
        pointers are not exposed.

        @param path  verb array, SkPoint array, weights, and SkPath::FillType to copy
        @return      SkPath copied by value

        example: https://fiddle.skia.org/c/@Path_copy_operator
    */
    SkPath& operator=(const SkPath& path);

    /** Compares a and b; returns true if SkPath::FillType, verb array, SkPoint array, and weights
        are equivalent.

        @param a  SkPath to compare
        @param b  SkPath to compare
        @return   true if SkPath pair are equivalent
    */
    friend PK_API bool operator==(const SkPath& a, const SkPath& b);

    /** Compares a and b; returns true if SkPath::FillType, verb array, SkPoint array, and weights
        are not equivalent.

        @param a  SkPath to compare
        @param b  SkPath to compare
        @return   true if SkPath pair are not equivalent
    */
    friend bool operator!=(const SkPath& a, const SkPath& b) {
        return !(a == b);
    }

    /** Returns true if SkPath contain equal verbs and equal weights.
        If SkPath contain one or more conics, the weights must match.

        conicTo() may add different verbs depending on conic weight, so it is not
        trivial to interpolate a pair of SkPath containing conics with different
        conic weight values.

        @param compare  SkPath to compare
        @return         true if SkPath verb array and weights are equivalent

        example: https://fiddle.skia.org/c/@Path_isInterpolatable
    */
    bool isInterpolatable(const SkPath& compare) const;

    /** Interpolates between SkPath with SkPoint array of equal size.
        Copy verb array and weights to out, and set out SkPoint array to a weighted
        average of this SkPoint array and ending SkPoint array, using the formula:
        (Path Point * weight) + ending Point * (1 - weight).

        weight is most useful when between zero (ending SkPoint array) and
        one (this Point_Array); will work with values outside of this
        range.

        interpolate() returns false and leaves out unchanged if SkPoint array is not
        the same size as ending SkPoint array. Call isInterpolatable() to check SkPath
        compatibility prior to calling interpolate().

        @param ending  SkPoint array averaged with this SkPoint array
        @param weight  contribution of this SkPoint array, and
                       one minus contribution of ending SkPoint array
        @param out     SkPath replaced by interpolated averages
        @return        true if SkPath contain same number of SkPoint

        example: https://fiddle.skia.org/c/@Path_interpolate
    */
    bool interpolate(const SkPath& ending, SkScalar weight, SkPath* out) const;

    /** Returns SkPathFillType, the rule used to fill SkPath.

        @return  current SkPathFillType setting
    */
    SkPathFillType getFillType() const { return (SkPathFillType)fFillType; }

    /** Sets FillType, the rule used to fill SkPath. While there is no check
        that ft is legal, values outside of FillType are not supported.
    */
    void setFillType(SkPathFillType ft) {
        fFillType = SkToU8(ft);
    }

    /** Returns if FillType describes area outside SkPath geometry. The inverse fill area
        extends indefinitely.

        @return  true if FillType is kInverseWinding or kInverseEvenOdd
    */
    bool isInverseFillType() const { return SkPathFillType_IsInverse(this->getFillType()); }

    /** Replaces FillType with its inverse. The inverse of FillType describes the area
        unmodified by the original FillType.
    */
    void toggleInverseFillType() {
        fFillType ^= 2;
    }

    /** Returns true if the path is convex. If necessary, it will first compute the convexity.
     */
    bool isConvex() const {
        return SkPathConvexity::kConvex == this->getConvexity();
    }

    /** Returns true if this path is recognized as an oval or circle.

        bounds receives bounds of oval.

        bounds is unmodified if oval is not found.

        @param bounds  storage for bounding SkRect of oval; may be nullptr
        @return        true if SkPath is recognized as an oval or circle

        example: https://fiddle.skia.org/c/@Path_isOval
    */
    bool isOval(SkRect* bounds) const;

    /** Returns true if path is representable as SkRRect.
        Returns false if path is representable as oval, circle, or SkRect.

        rrect receives bounds of SkRRect.

        rrect is unmodified if SkRRect is not found.

        @param rrect  storage for bounding SkRect of SkRRect; may be nullptr
        @return       true if SkPath contains only SkRRect

        example: https://fiddle.skia.org/c/@Path_isRRect
    */
    bool isRRect(SkRRect* rrect) const;

    /** Sets SkPath to its initial state.
        Removes verb array, SkPoint array, and weights, and sets FillType to kWinding.
        Internal storage associated with SkPath is released.

        @return  reference to SkPath

        example: https://fiddle.skia.org/c/@Path_reset
    */
    SkPath& reset();

    /** Sets SkPath to its initial state, preserving internal storage.
        Removes verb array, SkPoint array, and weights, and sets FillType to kWinding.
        Internal storage associated with SkPath is retained.

        Use rewind() instead of reset() if SkPath storage will be reused and performance
        is critical.

        @return  reference to SkPath

        example: https://fiddle.skia.org/c/@Path_rewind
    */
    SkPath& rewind();

    /** Returns if SkPath is empty.
        Empty SkPath may have FillType but has no SkPoint, SkPath::Verb, or conic weight.
        SkPath() constructs empty SkPath; reset() and rewind() make SkPath empty.

        @return  true if the path contains no SkPath::Verb array
    */
    bool isEmpty() const {
        return 0 == fPathRef->countVerbs();
    }

    /** Returns if contour is closed.
        Contour is closed if SkPath SkPath::Verb array was last modified by close(). When stroked,
        closed contour draws SkPaint::Join instead of SkPaint::Cap at first and last SkPoint.

        @return  true if the last contour ends with a kClose_Verb

        example: https://fiddle.skia.org/c/@Path_isLastContourClosed
    */
    bool isLastContourClosed() const;

    /** Returns true for finite SkPoint array values between negative PK_ScalarMax and
        positive PK_ScalarMax. Returns false for any SkPoint array value of
        PK_ScalarInfinity, PK_ScalarNegativeInfinity, or PK_ScalarNaN.

        @return  true if all SkPoint values are finite
    */
    bool isFinite() const {
        return fPathRef->isFinite();
    }

    /** Returns true if SkPath contains only one line;
        SkPath::Verb array has two entries: kMove_Verb, kLine_Verb.
        If SkPath contains one line and line is not nullptr, line is set to
        line start point and line end point.
        Returns false if SkPath is not one line; line is unaltered.

        @param line  storage for line. May be nullptr
        @return      true if SkPath contains exactly one line

        example: https://fiddle.skia.org/c/@Path_isLine
    */
    bool isLine(SkPoint line[2]) const;

    /** Returns the number of points in SkPath.
        SkPoint count is initially zero.

        @return  SkPath SkPoint array length

        example: https://fiddle.skia.org/c/@Path_countPoints
    */
    int countPoints() const;

    /** Returns SkPoint at index in SkPoint array. Valid range for index is
        0 to countPoints() - 1.
        Returns (0, 0) if index is out of range.

        @param index  SkPoint array element selector
        @return       SkPoint array value or (0, 0)

        example: https://fiddle.skia.org/c/@Path_getPoint
    */
    SkPoint getPoint(int index) const;

    /** Returns number of points in SkPath. Up to max points are copied.
        points may be nullptr; then, max must be zero.
        If max is greater than number of points, excess points storage is unaltered.

        @param points  storage for SkPath SkPoint array. May be nullptr
        @param max     maximum to copy; must be greater than or equal to zero
        @return        SkPath SkPoint array length

        example: https://fiddle.skia.org/c/@Path_getPoints
    */
    int getPoints(SkPoint points[], int max) const;

    /** Returns the number of verbs: kMove_Verb, kLine_Verb, kQuad_Verb, kConic_Verb,
        kCubic_Verb, and kClose_Verb; added to SkPath.

        @return  length of verb array

        example: https://fiddle.skia.org/c/@Path_countVerbs
    */
    int countVerbs() const;

    /** Returns the number of verbs in the path. Up to max verbs are copied. The
        verbs are copied as one byte per verb.

        @param verbs  storage for verbs, may be nullptr
        @param max    maximum number to copy into verbs
        @return       the actual number of verbs in the path

        example: https://fiddle.skia.org/c/@Path_getVerbs
    */
    int getVerbs(uint8_t verbs[], int max) const;

    /** Exchanges the verb array, SkPoint array, weights, and SkPath::FillType with other.
        Cached state is also exchanged. swap() internally exchanges pointers, so
        it is lightweight and does not allocate memory.

        swap() usage has largely been replaced by operator=(const SkPath& path).
        SkPath do not copy their content on assignment until they are written to,
        making assignment as efficient as swap().

        @param other  SkPath exchanged by value

        example: https://fiddle.skia.org/c/@Path_swap
    */
    void swap(SkPath& other);

    /** Returns minimum and maximum axes values of SkPoint array.
        Returns (0, 0, 0, 0) if SkPath contains no points. Returned bounds width and height may
        be larger or smaller than area affected when SkPath is drawn.

        SkRect returned includes all SkPoint added to SkPath, including SkPoint associated with
        kMove_Verb that define empty contours.

        @return  bounds of all SkPoint in SkPoint array
    */
    const SkRect& getBounds() const {
        return fPathRef->getBounds();
    }

    /** Returns true if rect is contained by SkPath.
        May return false when rect is contained by SkPath.

        For now, only returns true if SkPath has one contour and is convex.
        rect may share points and edges with SkPath and be contained.
        Returns true if rect is empty, that is, it has zero width or height; and
        the SkPoint or line described by rect is contained by SkPath.

        @param rect  SkRect, line, or SkPoint checked for containment
        @return      true if rect is contained

        example: https://fiddle.skia.org/c/@Path_conservativelyContainsRect
    */
    bool conservativelyContainsRect(const SkRect& rect) const;

    /** Grows SkPath verb array and SkPoint array to contain extraPtCount additional SkPoint.
        May improve performance and use less memory by
        reducing the number and size of allocations when creating SkPath.

        @param extraPtCount  number of additional SkPoint to allocate

        example: https://fiddle.skia.org/c/@Path_incReserve
    */
    void incReserve(int extraPtCount);

    /** Adds beginning of contour at SkPoint (x, y).

        @param x  x-axis value of contour start
        @param y  y-axis value of contour start
        @return   reference to SkPath

        example: https://fiddle.skia.org/c/@Path_moveTo
    */
    SkPath& moveTo(SkScalar x, SkScalar y);

    /** Adds beginning of contour at SkPoint p.

        @param p  contour start
        @return   reference to SkPath
    */
    SkPath& moveTo(const SkPoint& p) {
        return this->moveTo(p.fX, p.fY);
    }

    /** Adds line from last point to (x, y). If SkPath is empty, or last SkPath::Verb is
        kClose_Verb, last point is set to (0, 0) before adding line.

        lineTo() appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.
        lineTo() then appends kLine_Verb to verb array and (x, y) to SkPoint array.

        @param x  end of added line on x-axis
        @param y  end of added line on y-axis
        @return   reference to SkPath

        example: https://fiddle.skia.org/c/@Path_lineTo
    */
    SkPath& lineTo(SkScalar x, SkScalar y);

    /** Adds line from last point to SkPoint p. If SkPath is empty, or last SkPath::Verb is
        kClose_Verb, last point is set to (0, 0) before adding line.

        lineTo() first appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.
        lineTo() then appends kLine_Verb to verb array and SkPoint p to SkPoint array.

        @param p  end SkPoint of added line
        @return   reference to SkPath
    */
    SkPath& lineTo(const SkPoint& p) {
        return this->lineTo(p.fX, p.fY);
    }

    /** Adds quad from last point towards (x1, y1), to (x2, y2).
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding quad.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kQuad_Verb to verb array; and (x1, y1), (x2, y2)
        to SkPoint array.

        @param x1  control SkPoint of quad on x-axis
        @param y1  control SkPoint of quad on y-axis
        @param x2  end SkPoint of quad on x-axis
        @param y2  end SkPoint of quad on y-axis
        @return    reference to SkPath

        example: https://fiddle.skia.org/c/@Path_quadTo
    */
    SkPath& quadTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2);

    /** Adds quad from last point towards SkPoint p1, to SkPoint p2.
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding quad.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kQuad_Verb to verb array; and SkPoint p1, p2
        to SkPoint array.

        @param p1  control SkPoint of added quad
        @param p2  end SkPoint of added quad
        @return    reference to SkPath
    */
    SkPath& quadTo(const SkPoint& p1, const SkPoint& p2) {
        return this->quadTo(p1.fX, p1.fY, p2.fX, p2.fY);
    }

    /** Adds conic from last point towards (x1, y1), to (x2, y2), weighted by w.
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding conic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.

        If w is finite and not one, appends kConic_Verb to verb array;
        and (x1, y1), (x2, y2) to SkPoint array; and w to conic weights.

        If w is one, appends kQuad_Verb to verb array, and
        (x1, y1), (x2, y2) to SkPoint array.

        If w is not finite, appends kLine_Verb twice to verb array, and
        (x1, y1), (x2, y2) to SkPoint array.

        @param x1  control SkPoint of conic on x-axis
        @param y1  control SkPoint of conic on y-axis
        @param x2  end SkPoint of conic on x-axis
        @param y2  end SkPoint of conic on y-axis
        @param w   weight of added conic
        @return    reference to SkPath
    */
    SkPath& conicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                    SkScalar w);

    /** Adds conic from last point towards SkPoint p1, to SkPoint p2, weighted by w.
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding conic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.

        If w is finite and not one, appends kConic_Verb to verb array;
        and SkPoint p1, p2 to SkPoint array; and w to conic weights.

        If w is one, appends kQuad_Verb to verb array, and SkPoint p1, p2
        to SkPoint array.

        If w is not finite, appends kLine_Verb twice to verb array, and
        SkPoint p1, p2 to SkPoint array.

        @param p1  control SkPoint of added conic
        @param p2  end SkPoint of added conic
        @param w   weight of added conic
        @return    reference to SkPath
    */
    SkPath& conicTo(const SkPoint& p1, const SkPoint& p2, SkScalar w) {
        return this->conicTo(p1.fX, p1.fY, p2.fX, p2.fY, w);
    }

    /** Adds cubic from last point towards (x1, y1), then towards (x2, y2), ending at
        (x3, y3). If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to
        (0, 0) before adding cubic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kCubic_Verb to verb array; and (x1, y1), (x2, y2), (x3, y3)
        to SkPoint array.

        @param x1  first control SkPoint of cubic on x-axis
        @param y1  first control SkPoint of cubic on y-axis
        @param x2  second control SkPoint of cubic on x-axis
        @param y2  second control SkPoint of cubic on y-axis
        @param x3  end SkPoint of cubic on x-axis
        @param y3  end SkPoint of cubic on y-axis
        @return    reference to SkPath
    */
    SkPath& cubicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                    SkScalar x3, SkScalar y3);

    /** Adds cubic from last point towards SkPoint p1, then towards SkPoint p2, ending at
        SkPoint p3. If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to
        (0, 0) before adding cubic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kCubic_Verb to verb array; and SkPoint p1, p2, p3
        to SkPoint array.

        @param p1  first control SkPoint of cubic
        @param p2  second control SkPoint of cubic
        @param p3  end SkPoint of cubic
        @return    reference to SkPath
    */
    SkPath& cubicTo(const SkPoint& p1, const SkPoint& p2, const SkPoint& p3) {
        return this->cubicTo(p1.fX, p1.fY, p2.fX, p2.fY, p3.fX, p3.fY);
    }

    /** Appends kClose_Verb to SkPath. A closed contour connects the first and last SkPoint
        with line, forming a continuous loop. Open and closed contour draw the same
        with SkPaint::kFill_Style. With SkPaint::kStroke_Style, open contour draws
        SkPaint::Cap at contour start and end; closed contour draws
        SkPaint::Join at contour start and end.

        close() has no effect if SkPath is empty or last SkPath SkPath::Verb is kClose_Verb.

        @return  reference to SkPath

        example: https://fiddle.skia.org/c/@Path_close
    */
    SkPath& close();

    /** Approximates conic with quad array. Conic is constructed from start SkPoint p0,
        control SkPoint p1, end SkPoint p2, and weight w.
        Quad array is stored in pts; this storage is supplied by caller.
        Maximum quad count is 2 to the pow2.
        Every third point in array shares last SkPoint of previous quad and first SkPoint of
        next quad. Maximum pts storage size is given by:
        (1 + 2 * (1 << pow2)) * sizeof(SkPoint).

        Returns quad count used the approximation, which may be smaller
        than the number requested.

        conic weight determines the amount of influence conic control point has on the curve.
        w less than one represents an elliptical section. w greater than one represents
        a hyperbolic section. w equal to one represents a parabolic section.

        Two quad curves are sufficient to approximate an elliptical conic with a sweep
        of up to 90 degrees; in this case, set pow2 to one.

        @param p0    conic start SkPoint
        @param p1    conic control SkPoint
        @param p2    conic end SkPoint
        @param w     conic weight
        @param pts   storage for quad array
        @param pow2  quad count, as power of two, normally 0 to 5 (1 to 32 quad curves)
        @return      number of quad curves written to pts
    */
    static int ConvertConicToQuads(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2,
                                   SkScalar w, SkPoint pts[], int pow2);

    /** Returns true if SkPath is equivalent to SkRect when filled.
        If false: rect, isClosed, and direction are unchanged.
        If true: rect, isClosed, and direction are written to if not nullptr.

        rect may be smaller than the SkPath bounds. SkPath bounds may include kMove_Verb points
        that do not alter the area drawn by the returned rect.

        @param rect       storage for bounds of SkRect; may be nullptr
        @param isClosed   storage set to true if SkPath is closed; may be nullptr
        @param direction  storage set to SkRect direction; may be nullptr
        @return           true if SkPath contains SkRect

        example: https://fiddle.skia.org/c/@Path_isRect
    */
    bool isRect(SkRect* rect, bool* isClosed = nullptr, SkPathDirection* direction = nullptr) const;

    /** Adds a new contour to the path, defined by the rect, and wound in the
        specified direction. The verbs added to the path will be:

        kMove, kLine, kLine, kLine, kClose

        start specifies which corner to begin the contour:
            0: upper-left  corner
            1: upper-right corner
            2: lower-right corner
            3: lower-left  corner

        This start point also acts as the implied beginning of the subsequent,
        contour, if it does not have an explicit moveTo(). e.g.

            path.addRect(...)
            // if we don't say moveTo() here, we will use the rect's start point
            path.lineTo(...)

        @param rect   SkRect to add as a closed contour
        @param dir    SkPath::Direction to orient the new contour
        @param start  initial corner of SkRect to add
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addRect_2
     */
    SkPath& addRect(const SkRect& rect, SkPathDirection dir, unsigned start);

    SkPath& addRect(const SkRect& rect, SkPathDirection dir = SkPathDirection::kCW) {
        return this->addRect(rect, dir, 0);
    }

    SkPath& addRect(SkScalar left, SkScalar top, SkScalar right, SkScalar bottom,
                    SkPathDirection dir = SkPathDirection::kCW) {
        return this->addRect({left, top, right, bottom}, dir, 0);
    }

    /** Adds oval to path, appending kMove_Verb, four kConic_Verb, and kClose_Verb.
        Oval is upright ellipse bounded by SkRect oval with radii equal to half oval width
        and half oval height. Oval begins at (oval.fRight, oval.centerY()) and continues
        clockwise if dir is kCW_Direction, counterclockwise if dir is kCCW_Direction.

        @param oval  bounds of ellipse added
        @param dir   SkPath::Direction to wind ellipse
        @return      reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addOval
    */
    SkPath& addOval(const SkRect& oval, SkPathDirection dir = SkPathDirection::kCW);

    /** Adds oval to SkPath, appending kMove_Verb, four kConic_Verb, and kClose_Verb.
        Oval is upright ellipse bounded by SkRect oval with radii equal to half oval width
        and half oval height. Oval begins at start and continues
        clockwise if dir is kCW_Direction, counterclockwise if dir is kCCW_Direction.

        @param oval   bounds of ellipse added
        @param dir    SkPath::Direction to wind ellipse
        @param start  index of initial point of ellipse
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addOval_2
    */
    SkPath& addOval(const SkRect& oval, SkPathDirection dir, unsigned start);

    /** Adds circle centered at (x, y) of size radius to SkPath, appending kMove_Verb,
        four kConic_Verb, and kClose_Verb. Circle begins at: (x + radius, y), continuing
        clockwise if dir is kCW_Direction, and counterclockwise if dir is kCCW_Direction.

        Has no effect if radius is zero or negative.

        @param x       center of circle
        @param y       center of circle
        @param radius  distance from center to edge
        @param dir     SkPath::Direction to wind circle
        @return        reference to SkPath
    */
    SkPath& addCircle(SkScalar x, SkScalar y, SkScalar radius,
                      SkPathDirection dir = SkPathDirection::kCW);

    /** Appends SkRRect to SkPath, creating a new closed contour. SkRRect has bounds
        equal to rect; each corner is 90 degrees of an ellipse with radii (rx, ry). If
        dir is kCW_Direction, SkRRect starts at top-left of the lower-left corner and
        winds clockwise. If dir is kCCW_Direction, SkRRect starts at the bottom-left
        of the upper-left corner and winds counterclockwise.

        If either rx or ry is too large, rx and ry are scaled uniformly until the
        corners fit. If rx or ry is less than or equal to zero, addRoundRect() appends
        SkRect rect to SkPath.

        After appending, SkPath may be empty, or may contain: SkRect, oval, or SkRRect.

        @param rect  bounds of SkRRect
        @param rx    x-axis radius of rounded corners on the SkRRect
        @param ry    y-axis radius of rounded corners on the SkRRect
        @param dir   SkPath::Direction to wind SkRRect
        @return      reference to SkPath
    */
    SkPath& addRoundRect(const SkRect& rect, SkScalar rx, SkScalar ry,
                         SkPathDirection dir = SkPathDirection::kCW);

    /** Adds rrect to SkPath, creating a new closed contour. If
        dir is kCW_Direction, rrect starts at top-left of the lower-left corner and
        winds clockwise. If dir is kCCW_Direction, rrect starts at the bottom-left
        of the upper-left corner and winds counterclockwise.

        After appending, SkPath may be empty, or may contain: SkRect, oval, or SkRRect.

        @param rrect  bounds and radii of rounded rectangle
        @param dir    SkPath::Direction to wind SkRRect
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addRRect
    */
    SkPath& addRRect(const SkRRect& rrect, SkPathDirection dir = SkPathDirection::kCW);

    /** Adds rrect to SkPath, creating a new closed contour. If dir is kCW_Direction, rrect
        winds clockwise; if dir is kCCW_Direction, rrect winds counterclockwise.
        start determines the first point of rrect to add.

        @param rrect  bounds and radii of rounded rectangle
        @param dir    SkPath::Direction to wind SkRRect
        @param start  index of initial point of SkRRect
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addRRect_2
    */
    SkPath& addRRect(const SkRRect& rrect, SkPathDirection dir, unsigned start);

    /** Adds contour created from line array, adding (count - 1) line segments.
        Contour added starts at pts[0], then adds a line for every additional SkPoint
        in pts array. If close is true, appends kClose_Verb to SkPath, connecting
        pts[count - 1] and pts[0].

        If count is zero, append kMove_Verb to path.
        Has no effect if count is less than one.

        @param pts    array of line sharing end and start SkPoint
        @param count  length of SkPoint array
        @param close  true to add line connecting contour end and start
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addPoly
    */
    SkPath& addPoly(const SkPoint pts[], int count, bool close);

    /** Adds contour created from list. Contour added starts at list[0], then adds a line
        for every additional SkPoint in list. If close is true, appends kClose_Verb to SkPath,
        connecting last and first SkPoint in list.

        If list is empty, append kMove_Verb to path.

        @param list   array of SkPoint
        @param close  true to add line connecting contour end and start
        @return       reference to SkPath
    */
    SkPath& addPoly(const std::initializer_list<SkPoint>& list, bool close) {
        return this->addPoly(list.begin(), SkToInt(list.size()), close);
    }

    /** \enum SkPath::AddPathMode
        AddPathMode chooses how addPath() appends. Adding one SkPath to another can extend
        the last contour or start a new contour.
    */
    enum AddPathMode {
        kAppend_AddPathMode, //!< appended to destination unaltered
        kExtend_AddPathMode, //!< add line if prior contour is not closed
    };

    /** Appends src to SkPath, offset by (dx, dy).

        If mode is kAppend_AddPathMode, src verb array, SkPoint array, and conic weights are
        added unaltered. If mode is kExtend_AddPathMode, add line before appending
        verbs, SkPoint, and conic weights.

        @param src   SkPath verbs, SkPoint, and conic weights to add
        @param dx    offset added to src SkPoint array x-axis coordinates
        @param dy    offset added to src SkPoint array y-axis coordinates
        @param mode  kAppend_AddPathMode or kExtend_AddPathMode
        @return      reference to SkPath
    */
    SkPath& addPath(const SkPath& src, SkScalar dx, SkScalar dy,
                    AddPathMode mode = kAppend_AddPathMode);

    /** Appends src to SkPath.

        If mode is kAppend_AddPathMode, src verb array, SkPoint array, and conic weights are
        added unaltered. If mode is kExtend_AddPathMode, add line before appending
        verbs, SkPoint, and conic weights.

        @param src   SkPath verbs, SkPoint, and conic weights to add
        @param mode  kAppend_AddPathMode or kExtend_AddPathMode
        @return      reference to SkPath
    */
    SkPath& addPath(const SkPath& src, AddPathMode mode = kAppend_AddPathMode) {
        SkMatrix m;
        m.reset();
        return this->addPath(src, m, mode);
    }

    /** Appends src to SkPath, transformed by matrix. Transformed curves may have different
        verbs, SkPoint, and conic weights.

        If mode is kAppend_AddPathMode, src verb array, SkPoint array, and conic weights are
        added unaltered. If mode is kExtend_AddPathMode, add line before appending
        verbs, SkPoint, and conic weights.

        @param src     SkPath verbs, SkPoint, and conic weights to add
        @param matrix  transform applied to src
        @param mode    kAppend_AddPathMode or kExtend_AddPathMode
        @return        reference to SkPath
    */
    SkPath& addPath(const SkPath& src, const SkMatrix& matrix,
                    AddPathMode mode = kAppend_AddPathMode);

    /** Appends src to SkPath, from back to front.
        Reversed src always appends a new contour to SkPath.

        @param src  SkPath verbs, SkPoint, and conic weights to add
        @return     reference to SkPath

        example: https://fiddle.skia.org/c/@Path_reverseAddPath
    */
    SkPath& reverseAddPath(const SkPath& src);

    /** Transforms verb array, SkPoint array, and weight by matrix.
        transform may change verbs and increase their number.
        Transformed SkPath replaces dst; if dst is nullptr, original data
        is replaced.

        @param matrix  SkMatrix to apply to SkPath
        @param dst     overwritten, transformed copy of SkPath; may be nullptr
        @param pc      whether to apply perspective clipping

        example: https://fiddle.skia.org/c/@Path_transform
    */
    void transform(const SkMatrix& matrix, SkPath* dst) const;

    /** Transforms verb array, SkPoint array, and weight by matrix.
        transform may change verbs and increase their number.
        SkPath is replaced by transformed data.

        @param matrix  SkMatrix to apply to SkPath
        @param pc      whether to apply perspective clipping
    */
    void transform(const SkMatrix& matrix) {
        this->transform(matrix, this);
    }

    SkPath makeTransform(const SkMatrix& m) const {
        SkPath dst;
        this->transform(m, &dst);
        return dst;
    }

    /** Returns last point on SkPath in lastPt. Returns false if SkPoint array is empty,
        storing (0, 0) if lastPt is not nullptr.

        @param lastPt  storage for final SkPoint in SkPoint array; may be nullptr
        @return        true if SkPoint array contains one or more SkPoint

        example: https://fiddle.skia.org/c/@Path_getLastPt
    */
    bool getLastPt(SkPoint* lastPt) const;

    /** Sets last point to (x, y). If SkPoint array is empty, append kMove_Verb to
        verb array and append (x, y) to SkPoint array.

        @param x  set x-axis value of last point
        @param y  set y-axis value of last point

        example: https://fiddle.skia.org/c/@Path_setLastPt
    */
    void setLastPt(SkScalar x, SkScalar y);

    /** Sets the last point on the path. If SkPoint array is empty, append kMove_Verb to
        verb array and append p to SkPoint array.

        @param p  set value of last point
    */
    void setLastPt(const SkPoint& p) {
        this->setLastPt(p.fX, p.fY);
    }

    /** \enum SkPath::SegmentMask
        SegmentMask constants correspond to each drawing Verb type in SkPath; for
        instance, if SkPath only contains lines, only the kLine_SegmentMask bit is set.
    */
    enum SegmentMask {
        kLine_SegmentMask  = kLine_SkPathSegmentMask,
        kQuad_SegmentMask  = kQuad_SkPathSegmentMask,
        kConic_SegmentMask = kConic_SkPathSegmentMask,
        kCubic_SegmentMask = kCubic_SkPathSegmentMask,
    };

    /** Returns a mask, where each set bit corresponds to a SegmentMask constant
        if SkPath contains one or more verbs of that type.
        Returns zero if SkPath contains no lines, or curves: quads, conics, or cubics.

        getSegmentMasks() returns a cached result; it is very fast.

        @return  SegmentMask bits or zero
    */
    uint32_t getSegmentMasks() const { return fPathRef->getSegmentMasks(); }

    int toAATriangles(float tolerance, const SkRect& clipBounds, std::vector<float>* vertex) const;

    /** \enum SkPath::Verb
        Verb instructs SkPath how to interpret one or more SkPoint and optional conic weight;
        manage contour, and terminate SkPath.
    */
    enum Verb {
        kMove_Verb  = static_cast<int>(SkPathVerb::kMove),
        kLine_Verb  = static_cast<int>(SkPathVerb::kLine),
        kQuad_Verb  = static_cast<int>(SkPathVerb::kQuad),
        kConic_Verb = static_cast<int>(SkPathVerb::kConic),
        kCubic_Verb = static_cast<int>(SkPathVerb::kCubic),
        kClose_Verb = static_cast<int>(SkPathVerb::kClose),
        kDone_Verb  = kClose_Verb + 1
    };

    /** \class SkPath::Iter
        Iterates through verb array, and associated SkPoint array and conic weight.
        Provides options to treat open contours as closed, and to ignore
        degenerate data.
    */
    class PK_API Iter {
    public:

        /** Initializes SkPath::Iter with an empty SkPath. next() on SkPath::Iter returns
            kDone_Verb.
            Call setPath to initialize SkPath::Iter at a later time.

            @return  SkPath::Iter of empty SkPath

        example: https://fiddle.skia.org/c/@Path_Iter_Iter
        */
        Iter();

        /** Sets SkPath::Iter to return elements of verb array, SkPoint array, and conic weight in
            path. If forceClose is true, SkPath::Iter will add kLine_Verb and kClose_Verb after each
            open contour. path is not altered.

            @param path        SkPath to iterate
            @param forceClose  true if open contours generate kClose_Verb
            @return            SkPath::Iter of path

        example: https://fiddle.skia.org/c/@Path_Iter_const_SkPath
        */
        Iter(const SkPath& path, bool forceClose);

        /** Sets SkPath::Iter to return elements of verb array, SkPoint array, and conic weight in
            path. If forceClose is true, SkPath::Iter will add kLine_Verb and kClose_Verb after each
            open contour. path is not altered.

            @param path        SkPath to iterate
            @param forceClose  true if open contours generate kClose_Verb

        example: https://fiddle.skia.org/c/@Path_Iter_setPath
        */
        void setPath(const SkPath& path, bool forceClose);

        /** Returns next SkPath::Verb in verb array, and advances SkPath::Iter.
            When verb array is exhausted, returns kDone_Verb.

            Zero to four SkPoint are stored in pts, depending on the returned SkPath::Verb.

            @param pts  storage for SkPoint data describing returned SkPath::Verb
            @return     next SkPath::Verb from verb array

        example: https://fiddle.skia.org/c/@Path_RawIter_next
        */
        Verb next(SkPoint pts[4]);

        /** Returns conic weight if next() returned kConic_Verb.

            If next() has not been called, or next() did not return kConic_Verb,
            result is undefined.

            @return  conic weight for conic SkPoint returned by next()
        */
        SkScalar conicWeight() const { return *fConicWeights; }

        /** Returns true if last kLine_Verb returned by next() was generated
            by kClose_Verb. When true, the end point returned by next() is
            also the start point of contour.

            If next() has not been called, or next() did not return kLine_Verb,
            result is undefined.

            @return  true if last kLine_Verb was generated by kClose_Verb
        */
        bool isCloseLine() const { return SkToBool(fCloseLine); }

        /** Returns true if subsequent calls to next() return kClose_Verb before returning
            kMove_Verb. if true, contour SkPath::Iter is processing may end with kClose_Verb, or
            SkPath::Iter may have been initialized with force close set to true.

            @return  true if contour is closed

        example: https://fiddle.skia.org/c/@Path_Iter_isClosedContour
        */
        bool isClosedContour() const;

    private:
        const SkPoint*  fPts;
        const uint8_t*  fVerbs;
        const uint8_t*  fVerbStop;
        const SkScalar* fConicWeights;
        SkPoint         fMoveTo;
        SkPoint         fLastPt;
        bool            fForceClose;
        bool            fNeedClose;
        bool            fCloseLine;

        Verb autoClose(SkPoint pts[2]);
    };

private:
    /** \class SkPath::RangeIter
        Iterates through a raw range of path verbs, points, and conics. All values are returned
        unaltered.

        NOTE: This class will be moved into SkPathPriv once RangeIter is removed.
    */
    class RangeIter {
    public:
        RangeIter() = default;
        RangeIter(const uint8_t* verbs, const SkPoint* points, const SkScalar* weights)
                : fVerb(verbs), fPoints(points), fWeights(weights) {
        }
        bool operator!=(const RangeIter& that) const {
            return fVerb != that.fVerb;
        }
        bool operator==(const RangeIter& that) const {
            return fVerb == that.fVerb;
        }
        RangeIter& operator++() {
            auto verb = static_cast<SkPathVerb>(*fVerb++);
            fPoints += pts_advance_after_verb(verb);
            if (verb == SkPathVerb::kConic) {
                ++fWeights;
            }
            return *this;
        }
        RangeIter operator++(int) {
            RangeIter copy = *this;
            this->operator++();
            return copy;
        }
        SkPathVerb peekVerb() const {
            return static_cast<SkPathVerb>(*fVerb);
        }
        std::tuple<SkPathVerb, const SkPoint*, const SkScalar*> operator*() const {
            SkPathVerb verb = this->peekVerb();
            // We provide the starting point for beziers by peeking backwards from the current
            // point, which works fine as long as there is always a kMove before any geometry.
            // (SkPath::injectMoveToIfNeeded should have guaranteed this to be the case.)
            int backset = pts_backset_for_verb(verb);
            return {verb, fPoints + backset, fWeights};
        }
    private:
        constexpr static int pts_advance_after_verb(SkPathVerb verb) {
            switch (verb) {
                case SkPathVerb::kMove: return 1;
                case SkPathVerb::kLine: return 1;
                case SkPathVerb::kQuad: return 2;
                case SkPathVerb::kConic: return 2;
                case SkPathVerb::kCubic: return 3;
                case SkPathVerb::kClose: return 0;
            }
            PkUNREACHABLE;
        }
        constexpr static int pts_backset_for_verb(SkPathVerb verb) {
            switch (verb) {
                case SkPathVerb::kMove: return 0;
                case SkPathVerb::kLine: return -1;
                case SkPathVerb::kQuad: return -1;
                case SkPathVerb::kConic: return -1;
                case SkPathVerb::kCubic: return -1;
                case SkPathVerb::kClose: return -1;
            }
            PkUNREACHABLE;
        }
        const uint8_t* fVerb = nullptr;
        const SkPoint* fPoints = nullptr;
        const SkScalar* fWeights = nullptr;
    };
public:

    /** \class SkPath::RawIter
        Use Iter instead. This class will soon be removed and RangeIter will be made private.
    */
    class PK_API RawIter {
    public:

        /** Initializes RawIter with an empty SkPath. next() on RawIter returns kDone_Verb.
            Call setPath to initialize SkPath::Iter at a later time.

            @return  RawIter of empty SkPath
        */
        RawIter() {}

        /** Sets RawIter to return elements of verb array, SkPoint array, and conic weight in path.

            @param path  SkPath to iterate
            @return      RawIter of path
        */
        RawIter(const SkPath& path) {
            setPath(path);
        }

        /** Sets SkPath::Iter to return elements of verb array, SkPoint array, and conic weight in
            path.

            @param path  SkPath to iterate
        */
        void setPath(const SkPath&);

        /** Returns next SkPath::Verb in verb array, and advances RawIter.
            When verb array is exhausted, returns kDone_Verb.
            Zero to four SkPoint are stored in pts, depending on the returned SkPath::Verb.

            @param pts  storage for SkPoint data describing returned SkPath::Verb
            @return     next SkPath::Verb from verb array
        */
        Verb next(SkPoint[4]);

        /** Returns next SkPath::Verb, but does not advance RawIter.

            @return  next SkPath::Verb from verb array
        */
        Verb peek() const {
            return (fIter != fEnd) ? static_cast<Verb>(std::get<0>(*fIter)) : kDone_Verb;
        }

        /** Returns conic weight if next() returned kConic_Verb.

            If next() has not been called, or next() did not return kConic_Verb,
            result is undefined.

            @return  conic weight for conic SkPoint returned by next()
        */
        SkScalar conicWeight() const {
            return fConicWeight;
        }

    private:
        RangeIter fIter;
        RangeIter fEnd;
        SkScalar fConicWeight = 0;
        friend class SkPath;

    };

    /** Returns true if the point (x, y) is contained by SkPath, taking into
        account FillType.

        @param x  x-axis value of containment test
        @param y  y-axis value of containment test
        @return   true if SkPoint is in SkPath

        example: https://fiddle.skia.org/c/@Path_contains
    */
    bool contains(SkScalar x, SkScalar y) const;

private:
    SkPath(sk_sp<SkPathRef>, SkPathFillType, SkPathConvexity,
           SkPathFirstDirection firstDirection);

    sk_sp<SkPathRef>               fPathRef;
    int                            fLastMoveToIndex;
    mutable std::atomic<uint8_t>   fConvexity;      // SkPathConvexity
    mutable std::atomic<uint8_t>   fFirstDirection; // SkPathFirstDirection
    uint8_t                        fFillType    : 2;

    /** Resets all fields other than fPathRef to their initial 'empty' values.
     *  Assumes the caller has already emptied fPathRef.
     *  On Android increments fGenerationID without reseting it.
     */
    void resetFields();

    /** Sets all fields other than fPathRef to the values in 'that'.
     *  Assumes the caller has already set fPathRef.
     *  Doesn't change fGenerationID or fSourcePath on Android.
     */
    void copyFields(const SkPath& that);

    friend class Iter;
    friend class SkPathPriv;
    friend class SkPathStroker;

    /*  Append, in reverse order, the first contour of path, ignoring path's
        last point. If no moveTo() call has been made for this contour, the
        first point is automatically set to (0,0).
    */
    SkPath& reversePathTo(const SkPath&);

    // called before we add points for lineTo, quadTo, cubicTo, checking to see
    // if we need to inject a leading moveTo first
    //
    //  SkPath path; path.lineTo(...);   <--- need a leading moveTo(0, 0)
    // SkPath path; ... path.close(); path.lineTo(...) <-- need a moveTo(previous moveTo)
    //
    inline void injectMoveToIfNeeded();

    inline bool hasOnlyMoveTos() const;

    SkPathConvexity computeConvexity() const;

    // called by stroker to see if all points (in the last contour) are equal and worthy of a cap
    bool isZeroLengthSincePoint(int startPtIndex) const;

    /** Returns if the path can return a bound at no cost (true) or will have to
        perform some computation (false).
     */
    bool hasComputedBounds() const {
        return fPathRef->hasComputedBounds();
    }


    // 'rect' needs to be sorted
    void setBounds(const SkRect& rect) {
        SkPathRef::Editor ed(&fPathRef);

        ed.setBounds(rect);
    }

    void setPt(int index, SkScalar x, SkScalar y);

    SkPath& dirtyAfterEdit();

    // Bottlenecks for working with fConvexity and fFirstDirection.
    // Notice the setters are const... these are mutable atomic fields.
    void  setConvexity(SkPathConvexity) const;

    void setFirstDirection(SkPathFirstDirection) const;
    SkPathFirstDirection getFirstDirection() const;

    /** Returns the comvexity type, computing if needed. Never returns kUnknown.
        @return  path's convexity type (convex or concave)
    */
    SkPathConvexity getConvexity() const;

    SkPathConvexity getConvexityOrUnknown() const {
        return (SkPathConvexity)fConvexity.load(std::memory_order_relaxed);
    }

    /** Stores a convexity type for this path. This is what will be returned if
     *  getConvexityOrUnknown() is called. If you pass kUnknown, then if getContexityType()
     *  is called, the real convexity will be computed.
     *
     *  example: https://fiddle.skia.org/c/@Path_setConvexity
     */
    void setConvexity(SkPathConvexity convexity);


    friend class SkAutoPathBoundsUpdate;
    friend class SkAutoDisableDirectionCheck;
    friend class SkPathBuilder;
    friend class SkPathEdgeIter;
    friend class SkPathWriter;
    friend class SkOpBuilder;
};
}  // namespace pk
