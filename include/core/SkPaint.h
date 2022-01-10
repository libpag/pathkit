/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "include/core/SkScalar.h"
#include "include/core/SkRefCnt.h"

namespace pk {
struct SkRect;
class SkMatrix;
class SkPath;

/** \class SkPaint
    SkPaint controls options applied when drawing. SkPaint collects all
    options outside of the SkCanvas clip and SkCanvas matrix.

    Various options apply to strokes and fills, and images.

    SkPaint collects effects and filters that describe single-pass and multiple-pass
    algorithms that alter the drawing geometry, color, and transparency. For instance,
    SkPaint does not directly implement dashing or blur, but contains the objects that do so.
*/
class PK_API SkPaint {
public:

    /** Constructs SkPaint with default values.

        @return  default initialized SkPaint

        example: https://fiddle.skia.org/c/@Paint_empty_constructor
    */
    SkPaint();

    /** Makes a shallow copy of SkPaint. SkPathEffect, SkShader,
        SkMaskFilter, SkColorFilter, and SkImageFilter are shared
        between the original paint and the copy. Objects containing SkRefCnt increment
        their references by one.

        The referenced objects SkPathEffect, SkShader, SkMaskFilter, SkColorFilter,
        and SkImageFilter cannot be modified after they are created.
        This prevents objects with SkRefCnt from being modified once SkPaint refers to them.

        @param paint  original to copy
        @return       shallow copy of paint

        example: https://fiddle.skia.org/c/@Paint_copy_const_SkPaint
    */
    SkPaint(const SkPaint& paint);

    /** Implements a move constructor to avoid increasing the reference counts
        of objects referenced by the paint.

        After the call, paint is undefined, and can be safely destructed.

        @param paint  original to move
        @return       content of paint

        example: https://fiddle.skia.org/c/@Paint_move_SkPaint
    */
    SkPaint(SkPaint&& paint);

    /** Decreases SkPaint SkRefCnt of owned objects: SkPathEffect, SkShader,
        SkMaskFilter, SkColorFilter, and SkImageFilter. If the
        objects containing SkRefCnt go to zero, they are deleted.
    */
    ~SkPaint();

    /** Makes a shallow copy of SkPaint. SkPathEffect, SkShader,
        SkMaskFilter, SkColorFilter, and SkImageFilter are shared
        between the original paint and the copy. Objects containing SkRefCnt in the
        prior destination are decreased by one, and the referenced objects are deleted if the
        resulting count is zero. Objects containing SkRefCnt in the parameter paint
        are increased by one. paint is unmodified.

        @param paint  original to copy
        @return       content of paint

        example: https://fiddle.skia.org/c/@Paint_copy_operator
    */
    SkPaint& operator=(const SkPaint& paint);

    /** Moves the paint to avoid increasing the reference counts
        of objects referenced by the paint parameter. Objects containing SkRefCnt in the
        prior destination are decreased by one; those objects are deleted if the resulting count
        is zero.

        After the call, paint is undefined, and can be safely destructed.

        @param paint  original to move
        @return       content of paint

        example: https://fiddle.skia.org/c/@Paint_move_operator
    */
    SkPaint& operator=(SkPaint&& paint);

    /** Compares a and b, and returns true if a and b are equivalent. May return false
        if SkPathEffect, SkShader, SkMaskFilter, SkColorFilter,
        or SkImageFilter have identical contents but different pointers.

        @param a  SkPaint to compare
        @param b  SkPaint to compare
        @return   true if SkPaint pair are equivalent
    */
    PK_API friend bool operator==(const SkPaint& a, const SkPaint& b);

    /** Compares a and b, and returns true if a and b are not equivalent. May return true
        if SkPathEffect, SkShader, SkMaskFilter, SkColorFilter,
        or SkImageFilter have identical contents but different pointers.

        @param a  SkPaint to compare
        @param b  SkPaint to compare
        @return   true if SkPaint pair are not equivalent
    */
    friend bool operator!=(const SkPaint& a, const SkPaint& b) {
        return !(a == b);
    }

    /** Sets all SkPaint contents to their initial values. This is equivalent to replacing
        SkPaint with the result of SkPaint().

        example: https://fiddle.skia.org/c/@Paint_reset
    */
    void reset();

    /** \enum SkPaint::Style
        Set Style to fill, stroke, or both fill and stroke geometry.
        The stroke and fill
        share all paint attributes; for instance, they are drawn with the same color.

        Use kStrokeAndFill_Style to avoid hitting the same pixels twice with a stroke draw and
        a fill draw.
    */
    enum Style : uint8_t {
        kFill_Style,          //!< set to fill geometry
        kStroke_Style,        //!< set to stroke geometry
        kStrokeAndFill_Style, //!< sets to stroke and fill geometry
    };

    /** May be used to verify that SkPaint::Style is a legal value.
    */
    static constexpr int kStyleCount = kStrokeAndFill_Style + 1;

    /** Returns whether the geometry is filled, stroked, or filled and stroked.
    */
    Style getStyle() const { return (Style)fBitfields.fStyle; }

    /** Sets whether the geometry is filled, stroked, or filled and stroked.
        Has no effect if style is not a legal SkPaint::Style value.

        example: https://fiddle.skia.org/c/@Paint_setStyle
        example: https://fiddle.skia.org/c/@Stroke_Width
    */
    void setStyle(Style style);

    /**
     *  Set paint's style to kStroke if true, or kFill if false.
     */
    void setStroke(bool);

    /** Returns the thickness of the pen used by SkPaint to
        outline the shape.

        @return  zero for hairline, greater than zero for pen thickness
    */
    SkScalar getStrokeWidth() const { return fWidth; }

    /** Sets the thickness of the pen used by the paint to outline the shape.
        A stroke-width of zero is treated as "hairline" width. Hairlines are always exactly one
        pixel wide in device space (their thickness does not change as the canvas is scaled).
        Negative stroke-widths are invalid; setting a negative width will have no effect.

        @param width  zero thickness for hairline; greater than zero for pen thickness

        example: https://fiddle.skia.org/c/@Miter_Limit
        example: https://fiddle.skia.org/c/@Paint_setStrokeWidth
    */
    void setStrokeWidth(SkScalar width);

    /** Returns the limit at which a sharp corner is drawn beveled.

        @return  zero and greater miter limit
    */
    SkScalar getStrokeMiter() const { return fMiterLimit; }

    /** Sets the limit at which a sharp corner is drawn beveled.
        Valid values are zero and greater.
        Has no effect if miter is less than zero.

        @param miter  zero and greater miter limit

        example: https://fiddle.skia.org/c/@Paint_setStrokeMiter
    */
    void setStrokeMiter(SkScalar miter);

    /** \enum SkPaint::Cap
        Cap draws at the beginning and end of an open path contour.
    */
    enum Cap {
        kButt_Cap,                  //!< no stroke extension
        kRound_Cap,                 //!< adds circle
        kSquare_Cap,                //!< adds square
        kLast_Cap    = kSquare_Cap, //!< largest Cap value
        kDefault_Cap = kButt_Cap,   //!< equivalent to kButt_Cap
    };

    /** May be used to verify that SkPaint::Cap is a legal value.
    */
    static constexpr int kCapCount = kLast_Cap + 1;

    /** \enum SkPaint::Join
        Join specifies how corners are drawn when a shape is stroked. Join
        affects the four corners of a stroked rectangle, and the connected segments in a
        stroked path.

        Choose miter join to draw sharp corners. Choose round join to draw a circle with a
        radius equal to the stroke width on top of the corner. Choose bevel join to minimally
        connect the thick strokes.

        The fill path constructed to describe the stroked path respects the join setting but may
        not contain the actual join. For instance, a fill path constructed with round joins does
        not necessarily include circles at each connected segment.
    */
    enum Join : uint8_t {
        kMiter_Join,                 //!< extends to miter limit
        kRound_Join,                 //!< adds circle
        kBevel_Join,                 //!< connects outside edges
        kLast_Join    = kBevel_Join, //!< equivalent to the largest value for Join
        kDefault_Join = kMiter_Join, //!< equivalent to kMiter_Join
    };

    /** May be used to verify that SkPaint::Join is a legal value.
    */
    static constexpr int kJoinCount = kLast_Join + 1;

    /** Returns the geometry drawn at the beginning and end of strokes.
    */
    Cap getStrokeCap() const { return (Cap)fBitfields.fCapType; }

    /** Sets the geometry drawn at the beginning and end of strokes.

        example: https://fiddle.skia.org/c/@Paint_setStrokeCap_a
        example: https://fiddle.skia.org/c/@Paint_setStrokeCap_b
    */
    void setStrokeCap(Cap cap);

    /** Returns the geometry drawn at the corners of strokes.
    */
    Join getStrokeJoin() const { return (Join)fBitfields.fJoinType; }

    /** Sets the geometry drawn at the corners of strokes.

        example: https://fiddle.skia.org/c/@Paint_setStrokeJoin
    */
    void setStrokeJoin(Join join);

    /** Returns the filled equivalent of the stroked path.

        @param src       SkPath read to create a filled version
        @param dst       resulting SkPath; may be the same as src, but may not be nullptr
        @param cullRect  optional limit passed to SkPathEffect
        @param resScale  if > 1, increase precision, else if (0 < resScale < 1) reduce precision
                         to favor speed and size
        @return          true if the path represents style fill, or false if it represents hairline
    */
    bool getFillPath(const SkPath& src, SkPath* dst, const SkRect* cullRect,
                     SkScalar resScale = 1) const;

    bool getFillPath(const SkPath& src, SkPath* dst, const SkRect* cullRect,
                     const SkMatrix& ctm) const;

    /** Returns the filled equivalent of the stroked path.

        Replaces dst with the src path modified by SkPathEffect and style stroke.
        SkPathEffect, if any, is not culled. stroke width is created with default precision.

        @param src  SkPath read to create a filled version
        @param dst  resulting SkPath dst may be the same as src, but may not be nullptr
        @return     true if the path represents style fill, or false if it represents hairline
    */
    bool getFillPath(const SkPath& src, SkPath* dst) const {
        return this->getFillPath(src, dst, nullptr, 1);
    }

private:
    SkScalar        fWidth;
    SkScalar        fMiterLimit;
    union {
        struct {
            unsigned    fAntiAlias : 1;
            unsigned    fDither : 1;
            unsigned    fCapType : 2;
            unsigned    fJoinType : 2;
            unsigned    fStyle : 2;
            unsigned    fPadding : 24;  // 24 == 32 -1-1-2-2-2
        } fBitfields;
        uint32_t fBitfieldsUInt;
    };

    friend class SkPaintPriv;
};
}  // namespace pk
