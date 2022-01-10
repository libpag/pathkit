/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"

namespace pk {
class SkPath;
class SkMatrix;
class SkString;

/** \class SkRRect
    SkRRect describes a rounded rectangle with a bounds and a pair of radii for each corner.
    The bounds and radii can be set so that SkRRect describes: a rectangle with sharp corners;
    a circle; an oval; or a rectangle with one or more rounded corners.

    SkRRect allows implementing CSS properties that describe rounded corners.
    SkRRect may have up to eight different radii, one for each axis on each of its four
    corners.

    SkRRect may modify the provided parameters when initializing bounds and radii.
    If either axis radii is zero or less: radii are stored as zero; corner is square.
    If corner curves overlap, radii are proportionally reduced to fit within bounds.
*/
class PK_API SkRRect {
public:

    /** Initializes bounds at (0, 0), the origin, with zero width and height.
        Initializes corner radii to (0, 0), and sets type of kEmpty_Type.

        @return  empty SkRRect
    */
    SkRRect() = default;

    /** Initializes to copy of rrect bounds and corner radii.

        @param rrect  bounds and corner to copy
        @return       copy of rrect
    */
    SkRRect(const SkRRect& rrect) = default;

    /** Copies rrect bounds and corner radii.

        @param rrect  bounds and corner to copy
        @return       copy of rrect
    */
    SkRRect& operator=(const SkRRect& rrect) = default;

    /** \enum SkRRect::Type
        Type describes possible specializations of SkRRect. Each Type is
        exclusive; a SkRRect may only have one type.

        Type members become progressively less restrictive; larger values of
        Type have more degrees of freedom than smaller values.
    */
    enum Type {
        kEmpty_Type,                     //!< zero width or height
        kRect_Type,                      //!< non-zero width and height, and zeroed radii
        kOval_Type,                      //!< non-zero width and height filled with radii
        kSimple_Type,                    //!< non-zero width and height with equal radii
        kNinePatch_Type,                 //!< non-zero width and height with axis-aligned radii
        kComplex_Type,                   //!< non-zero width and height with arbitrary radii
        kLastType       = kComplex_Type, //!< largest Type value
    };

    Type getType() const {
        return static_cast<Type>(fType);
    }

    Type type() const { return this->getType(); }

    inline bool isEmpty() const { return kEmpty_Type == this->getType(); }
    inline bool isRect() const { return kRect_Type == this->getType(); }
    inline bool isOval() const { return kOval_Type == this->getType(); }
    inline bool isSimple() const { return kSimple_Type == this->getType(); }
    inline bool isNinePatch() const { return kNinePatch_Type == this->getType(); }
    inline bool isComplex() const { return kComplex_Type == this->getType(); }

    /** Returns span on the x-axis. This does not check if result fits in 32-bit float;
        result may be infinity.

        @return  rect().fRight minus rect().fLeft
    */
    SkScalar width() const { return fRect.width(); }

    /** Returns span on the y-axis. This does not check if result fits in 32-bit float;
        result may be infinity.

        @return  rect().fBottom minus rect().fTop
    */
    SkScalar height() const { return fRect.height(); }

    /** Returns top-left corner radii. If type() returns kEmpty_Type, kRect_Type,
        kOval_Type, or kSimple_Type, returns a value representative of all corner radii.
        If type() returns kNinePatch_Type or kComplex_Type, at least one of the
        remaining three corners has a different value.

        @return  corner radii for simple types
    */
    SkVector getSimpleRadii() const { return fRadii[0]; }

    /** Sets bounds to zero width and height at (0, 0), the origin. Sets
        corner radii to zero and sets type to kEmpty_Type.
    */
    void setEmpty() { *this = SkRRect(); }

    /** Sets bounds to sorted rect, and sets corner radii to zero.
        If set bounds has width and height, and sets type to kRect_Type;
        otherwise, sets type to kEmpty_Type.

        @param rect  bounds to set
    */
    void setRect(const SkRect& rect) {
        if (!this->initializeRect(rect)) {
            return;
        }

        memset(fRadii, 0, sizeof(fRadii));
        fType = kRect_Type;
    }

    /** Initializes bounds at (0, 0), the origin, with zero width and height.
        Initializes corner radii to (0, 0), and sets type of kEmpty_Type.

        @return  empty SkRRect
    */
    static SkRRect MakeEmpty() { return SkRRect(); }

    /** Initializes to copy of r bounds and zeroes corner radii.

        @param r  bounds to copy
        @return   copy of r
    */
    static SkRRect MakeRect(const SkRect& r) {
        SkRRect rr;
        rr.setRect(r);
        return rr;
    }

    /** Sets bounds to oval, x-axis radii to half oval.width(), and all y-axis radii
        to half oval.height(). If oval bounds is empty, sets to kEmpty_Type.
        Otherwise, sets to kOval_Type.

        @param oval  bounds of oval
        @return      oval
    */
    static SkRRect MakeOval(const SkRect& oval) {
        SkRRect rr;
        rr.setOval(oval);
        return rr;
    }

    /** Sets to rounded rectangle with the same radii for all four corners.
        If rect is empty, sets to kEmpty_Type.
        Otherwise, if xRad and yRad are zero, sets to kRect_Type.
        Otherwise, if xRad is at least half rect.width() and yRad is at least half
        rect.height(), sets to kOval_Type.
        Otherwise, sets to kSimple_Type.

        @param rect  bounds of rounded rectangle
        @param xRad  x-axis radius of corners
        @param yRad  y-axis radius of corners
        @return      rounded rectangle
    */
    static SkRRect MakeRectXY(const SkRect& rect, SkScalar xRad, SkScalar yRad) {
        SkRRect rr;
        rr.setRectXY(rect, xRad, yRad);
        return rr;
    }

    /** Sets bounds to oval, x-axis radii to half oval.width(), and all y-axis radii
        to half oval.height(). If oval bounds is empty, sets to kEmpty_Type.
        Otherwise, sets to kOval_Type.

        @param oval  bounds of oval
    */
    void setOval(const SkRect& oval);

    /** Sets to rounded rectangle with the same radii for all four corners.
        If rect is empty, sets to kEmpty_Type.
        Otherwise, if xRad or yRad is zero, sets to kRect_Type.
        Otherwise, if xRad is at least half rect.width() and yRad is at least half
        rect.height(), sets to kOval_Type.
        Otherwise, sets to kSimple_Type.

        @param rect  bounds of rounded rectangle
        @param xRad  x-axis radius of corners
        @param yRad  y-axis radius of corners

        example: https://fiddle.skia.org/c/@RRect_setRectXY
    */
    void setRectXY(const SkRect& rect, SkScalar xRad, SkScalar yRad);

    /** Sets bounds to rect. Sets radii array for individual control of all for corners.

        If rect is empty, sets to kEmpty_Type.
        Otherwise, if one of each corner radii are zero, sets to kRect_Type.
        Otherwise, if all x-axis radii are equal and at least half rect.width(), and
        all y-axis radii are equal at least half rect.height(), sets to kOval_Type.
        Otherwise, if all x-axis radii are equal, and all y-axis radii are equal,
        sets to kSimple_Type. Otherwise, sets to kNinePatch_Type.

        @param rect   bounds of rounded rectangle
        @param radii  corner x-axis and y-axis radii

        example: https://fiddle.skia.org/c/@RRect_setRectRadii
    */
    void setRectRadii(const SkRect& rect, const SkVector radii[4]);

    /** \enum SkRRect::Corner
        The radii are stored: top-left, top-right, bottom-right, bottom-left.
    */
    enum Corner {
        kUpperLeft_Corner,  //!< index of top-left corner radii
        kUpperRight_Corner, //!< index of top-right corner radii
        kLowerRight_Corner, //!< index of bottom-right corner radii
        kLowerLeft_Corner,  //!< index of bottom-left corner radii
    };

    /** Returns bounds. Bounds may have zero width or zero height. Bounds right is
        greater than or equal to left; bounds bottom is greater than or equal to top.
        Result is identical to getBounds().

        @return  bounding box
    */
    const SkRect& rect() const { return fRect; }

    /** Returns scalar pair for radius of curve on x-axis and y-axis for one corner.
        Both radii may be zero. If not zero, both are positive and finite.

        @return        x-axis and y-axis radii for one corner
    */
    SkVector radii(Corner corner) const { return fRadii[corner]; }

    /** Returns bounds. Bounds may have zero width or zero height. Bounds right is
        greater than or equal to left; bounds bottom is greater than or equal to top.
        Result is identical to rect().

        @return  bounding box
    */
    const SkRect& getBounds() const { return fRect; }

    /** Returns true if bounds and radii in a are equal to bounds and radii in b.

        a and b are not equal if either contain NaN. a and b are equal if members
        contain zeroes with different signs.

        @param a  SkRect bounds and radii to compare
        @param b  SkRect bounds and radii to compare
        @return   true if members are equal
    */
    friend bool operator==(const SkRRect& a, const SkRRect& b) {
        return a.fRect == b.fRect && SkScalarsEqual(&a.fRadii[0].fX, &b.fRadii[0].fX, 8);
    }

    /** Returns true if bounds and radii in a are not equal to bounds and radii in b.

        a and b are not equal if either contain NaN. a and b are equal if members
        contain zeroes with different signs.

        @param a  SkRect bounds and radii to compare
        @param b  SkRect bounds and radii to compare
        @return   true if members are not equal
    */
    friend bool operator!=(const SkRRect& a, const SkRRect& b) {
        return a.fRect != b.fRect || !SkScalarsEqual(&a.fRadii[0].fX, &b.fRadii[0].fX, 8);
    }

    /** Returns true if bounds and radii values are finite and describe a SkRRect
        SkRRect::Type that matches getType(). All SkRRect methods construct valid types,
        even if the input values are not valid. Invalid SkRRect data can only
        be generated by corrupting memory.

        @return  true if bounds and radii match type()

        example: https://fiddle.skia.org/c/@RRect_isValid
    */
    bool isValid() const;

private:
    static bool AreRectAndRadiiValid(const SkRect&, const SkVector[4]);

    /**
     * Initializes fRect. If the passed in rect is not finite or empty the rrect will be fully
     * initialized and false is returned. Otherwise, just fRect is initialized and true is returned.
     */
    bool initializeRect(const SkRect&);

    void computeType();
    // Returns true if the radii had to be scaled to fit rect
    bool scaleRadii();

    SkRect fRect = SkRect::MakeEmpty();
    // Radii order is UL, UR, LR, LL. Use Corner enum to index into fRadii[]
    SkVector fRadii[4] = {{0, 0}, {0, 0}, {0,0}, {0,0}};
    // use an explicitly sized type so we're sure the class is dense (no uninitialized bytes)
    int32_t fType = kEmpty_Type;
    // TODO: add padding so we can use memcpy for flattening and not copy uninitialized data

    // to access fRadii directly
    friend class SkPath;
};
}  // namespace pk
