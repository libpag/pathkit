/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkRRect.h"
#include "src/core/SkRectPriv.h"
#include "src/core/SkScaleToSides.h"

namespace pk {
///////////////////////////////////////////////////////////////////////////////

void SkRRect::setOval(const SkRect& oval) {
  if (!this->initializeRect(oval)) {
    return;
  }

  SkScalar xRad = SkRectPriv::HalfWidth(fRect);
  SkScalar yRad = SkRectPriv::HalfHeight(fRect);

  if (xRad == 0.0f || yRad == 0.0f) {
    // All the corners will be square
    memset(fRadii, 0, sizeof(fRadii));
    fType = kRect_Type;
  } else {
    for (int i = 0; i < 4; ++i) {
      fRadii[i].set(xRad, yRad);
    }
    fType = kOval_Type;
  }

  PkASSERT(this->isValid());
}

void SkRRect::setRectXY(const SkRect& rect, SkScalar xRad, SkScalar yRad) {
  if (!this->initializeRect(rect)) {
    return;
  }

  if (!SkScalarsAreFinite(xRad, yRad)) {
    xRad = yRad = 0;  // devolve into a simple rect
  }

  if (fRect.width() < xRad + xRad || fRect.height() < yRad + yRad) {
    // At most one of these two divides will be by zero, and neither numerator is zero.
    SkScalar scale = std::min(sk_ieee_float_divide(fRect.width(), xRad + xRad),
                              sk_ieee_float_divide(fRect.height(), yRad + yRad));
    xRad *= scale;
    yRad *= scale;
  }

  if (xRad <= 0 || yRad <= 0) {
    // all corners are square in this case
    this->setRect(rect);
    return;
  }

  for (int i = 0; i < 4; ++i) {
    fRadii[i].set(xRad, yRad);
  }
  fType = kSimple_Type;
  if (xRad >= PkScalarHalf(fRect.width()) && yRad >= PkScalarHalf(fRect.height())) {
    fType = kOval_Type;
    // TODO: assert that all the x&y radii are already W/2 & H/2
  }
}

// These parameters intentionally double. Apropos crbug.com/463920, if one of the
// radii is huge while the other is small, single precision math can completely
// miss the fact that a scale is required.
static double compute_min_scale(double rad1, double rad2, double limit, double curMin) {
  if ((rad1 + rad2) > limit) {
    return std::min(curMin, limit / (rad1 + rad2));
  }
  return curMin;
}

static bool clamp_to_zero(SkVector radii[4]) {
  bool allCornersSquare = true;

  // Clamp negative radii to zero
  for (int i = 0; i < 4; ++i) {
    if (radii[i].fX <= 0 || radii[i].fY <= 0) {
      // In this case we are being a little fast & loose. Since one of
      // the radii is 0 the corner is square. However, the other radii
      // could still be non-zero and play in the global scale factor
      // computation.
      radii[i].fX = 0;
      radii[i].fY = 0;
    } else {
      allCornersSquare = false;
    }
  }

  return allCornersSquare;
}

void SkRRect::setRectRadii(const SkRect& rect, const SkVector radii[4]) {
  if (!this->initializeRect(rect)) {
    return;
  }

  if (!SkScalarsAreFinite(&radii[0].fX, 8)) {
    this->setRect(rect);  // devolve into a simple rect
    return;
  }

  memcpy(fRadii, radii, sizeof(fRadii));

  if (clamp_to_zero(fRadii)) {
    this->setRect(rect);
    return;
  }

  this->scaleRadii();

  if (!this->isValid()) {
    this->setRect(rect);
    return;
  }
}

bool SkRRect::initializeRect(const SkRect& rect) {
  // Check this before sorting because sorting can hide nans.
  if (!rect.isFinite()) {
    *this = SkRRect();
    return false;
  }
  fRect = rect.makeSorted();
  if (fRect.isEmpty()) {
    memset(fRadii, 0, sizeof(fRadii));
    fType = kEmpty_Type;
    return false;
  }
  return true;
}

// If we can't distinguish one of the radii relative to the other, force it to zero so it
// doesn't confuse us later. See crbug.com/850350
//
static void flush_to_zero(SkScalar& a, SkScalar& b) {
  if (a + b == a) {
    b = 0;
  } else if (a + b == b) {
    a = 0;
  }
}

bool SkRRect::scaleRadii() {
  // Proportionally scale down all radii to fit. Find the minimum ratio
  // of a side and the radii on that side (for all four sides) and use
  // that to scale down _all_ the radii. This algorithm is from the
  // W3 spec (http://www.w3.org/TR/css3-background/) section 5.5 - Overlapping
  // Curves:
  // "Let f = min(Li/Si), where i is one of { top, right, bottom, left },
  //   Si is the sum of the two corresponding radii of the corners on side i,
  //   and Ltop = Lbottom = the width of the box,
  //   and Lleft = Lright = the height of the box.
  // If f < 1, then all corner radii are reduced by multiplying them by f."
  double scale = 1.0;

  // The sides of the rectangle may be larger than a float.
  double width = (double)fRect.fRight - (double)fRect.fLeft;
  double height = (double)fRect.fBottom - (double)fRect.fTop;
  scale = compute_min_scale(fRadii[0].fX, fRadii[1].fX, width, scale);
  scale = compute_min_scale(fRadii[1].fY, fRadii[2].fY, height, scale);
  scale = compute_min_scale(fRadii[2].fX, fRadii[3].fX, width, scale);
  scale = compute_min_scale(fRadii[3].fY, fRadii[0].fY, height, scale);

  flush_to_zero(fRadii[0].fX, fRadii[1].fX);
  flush_to_zero(fRadii[1].fY, fRadii[2].fY);
  flush_to_zero(fRadii[2].fX, fRadii[3].fX);
  flush_to_zero(fRadii[3].fY, fRadii[0].fY);

  if (scale < 1.0) {
    SkScaleToSides::AdjustRadii(width, scale, &fRadii[0].fX, &fRadii[1].fX);
    SkScaleToSides::AdjustRadii(height, scale, &fRadii[1].fY, &fRadii[2].fY);
    SkScaleToSides::AdjustRadii(width, scale, &fRadii[2].fX, &fRadii[3].fX);
    SkScaleToSides::AdjustRadii(height, scale, &fRadii[3].fY, &fRadii[0].fY);
  }

  // adjust radii may set x or y to zero; set companion to zero as well
  clamp_to_zero(fRadii);

  // May be simple, oval, or complex, or become a rect/empty if the radii adjustment made them 0
  this->computeType();

  return scale < 1.0;
}

static bool radii_are_nine_patch(const SkVector radii[4]) {
  return radii[SkRRect::kUpperLeft_Corner].fX == radii[SkRRect::kLowerLeft_Corner].fX &&
         radii[SkRRect::kUpperLeft_Corner].fY == radii[SkRRect::kUpperRight_Corner].fY &&
         radii[SkRRect::kUpperRight_Corner].fX == radii[SkRRect::kLowerRight_Corner].fX &&
         radii[SkRRect::kLowerLeft_Corner].fY == radii[SkRRect::kLowerRight_Corner].fY;
}

// There is a simplified version of this method in setRectXY
void SkRRect::computeType() {
  if (fRect.isEmpty()) {
    PkASSERT(fRect.isSorted());
    for (size_t i = 0; i < PK_ARRAY_COUNT(fRadii); ++i) {
      PkASSERT((fRadii[i] == SkVector{0, 0}));
    }
    fType = kEmpty_Type;
    PkASSERT(this->isValid());
    return;
  }

  bool allRadiiEqual = true; // are all x radii equal and all y radii?
  bool allCornersSquare = 0 == fRadii[0].fX || 0 == fRadii[0].fY;

  for (int i = 1; i < 4; ++i) {
    if (0 != fRadii[i].fX && 0 != fRadii[i].fY) {
      // if either radius is zero the corner is square so both have to
      // be non-zero to have a rounded corner
      allCornersSquare = false;
    }
    if (fRadii[i].fX != fRadii[i - 1].fX || fRadii[i].fY != fRadii[i - 1].fY) {
      allRadiiEqual = false;
    }
  }

  if (allCornersSquare) {
    fType = kRect_Type;
    PkASSERT(this->isValid());
    return;
  }

  if (allRadiiEqual) {
    if (fRadii[0].fX >= PkScalarHalf(fRect.width()) &&
        fRadii[0].fY >= PkScalarHalf(fRect.height())) {
      fType = kOval_Type;
    } else {
      fType = kSimple_Type;
    }
    PkASSERT(this->isValid());
    return;
  }

  if (radii_are_nine_patch(fRadii)) {
    fType = kNinePatch_Type;
  } else {
    fType = kComplex_Type;
  }

  if (!this->isValid()) {
    this->setRect(this->rect());
    PkASSERT(this->isValid());
  }
}

///////////////////////////////////////////////////////////////////////////////

/**
 *  We need all combinations of predicates to be true to have a "safe" radius value.
 */
static bool are_radius_check_predicates_valid(SkScalar rad, SkScalar min, SkScalar max) {
  return (min <= max) && (rad <= max - min) && (min + rad <= max) && (max - rad >= min) &&
         rad >= 0;
}

bool SkRRect::isValid() const {
  if (!AreRectAndRadiiValid(fRect, fRadii)) {
    return false;
  }

  bool allRadiiZero = (0 == fRadii[0].fX && 0 == fRadii[0].fY);
  bool allCornersSquare = (0 == fRadii[0].fX || 0 == fRadii[0].fY);
  bool allRadiiSame = true;

  for (int i = 1; i < 4; ++i) {
    if (0 != fRadii[i].fX || 0 != fRadii[i].fY) {
      allRadiiZero = false;
    }

    if (fRadii[i].fX != fRadii[i - 1].fX || fRadii[i].fY != fRadii[i - 1].fY) {
      allRadiiSame = false;
    }

    if (0 != fRadii[i].fX && 0 != fRadii[i].fY) {
      allCornersSquare = false;
    }
  }
  bool patchesOfNine = radii_are_nine_patch(fRadii);

  if (fType < 0 || fType > kLastType) {
    return false;
  }

  switch (fType) {
    case kEmpty_Type:
      if (!fRect.isEmpty() || !allRadiiZero || !allRadiiSame || !allCornersSquare) {
        return false;
      }
      break;
    case kRect_Type:
      if (fRect.isEmpty() || !allRadiiZero || !allRadiiSame || !allCornersSquare) {
        return false;
      }
      break;
    case kOval_Type:
      if (fRect.isEmpty() || allRadiiZero || !allRadiiSame || allCornersSquare) {
        return false;
      }

      for (int i = 0; i < 4; ++i) {
        if (!SkScalarNearlyEqual(fRadii[i].fX, SkRectPriv::HalfWidth(fRect)) ||
            !SkScalarNearlyEqual(fRadii[i].fY, SkRectPriv::HalfHeight(fRect))) {
          return false;
        }
      }
      break;
    case kSimple_Type:
      if (fRect.isEmpty() || allRadiiZero || !allRadiiSame || allCornersSquare) {
        return false;
      }
      break;
    case kNinePatch_Type:
      if (fRect.isEmpty() || allRadiiZero || allRadiiSame || allCornersSquare ||
          !patchesOfNine) {
        return false;
      }
      break;
    case kComplex_Type:
      if (fRect.isEmpty() || allRadiiZero || allRadiiSame || allCornersSquare ||
          patchesOfNine) {
        return false;
      }
      break;
  }

  return true;
}

bool SkRRect::AreRectAndRadiiValid(const SkRect& rect, const SkVector radii[4]) {
  if (!rect.isFinite() || !rect.isSorted()) {
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    if (!are_radius_check_predicates_valid(radii[i].fX, rect.fLeft, rect.fRight) ||
        !are_radius_check_predicates_valid(radii[i].fY, rect.fTop, rect.fBottom)) {
      return false;
    }
  }
  return true;
}
///////////////////////////////////////////////////////////////////////////////
}  // namespace pk