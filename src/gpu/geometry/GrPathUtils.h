/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrPathUtils_DEFINED
#define GrPathUtils_DEFINED

#include "include/core/SkPoint.h"

namespace pk {
/**
 *  Utilities for evaluating paths.
 */
namespace GrPathUtils {

// When tessellating curved paths into linear segments, this defines the maximum distance in screen
// space which a segment may deviate from the mathematically correct value. Above this value, the
// segment will be subdivided.
// This value was chosen to approximate the supersampling accuracy of the raster path (16 samples,
// or one quarter pixel).
static const SkScalar kDefaultTolerance = PkDoubleToScalar(0.25);

// We guarantee that no quad or cubic will ever produce more than this many points
static const int kMaxPointsPerCurve = 1 << 10;

// Returns the maximum number of vertices required when using a recursive chopping algorithm to
// linearize the cubic Bezier (e.g. generateQuadraticPoints below) to the given error tolerance.
// This is a power of two and will not exceed kMaxPointsPerCurve.
uint32_t cubicPointCount(const SkPoint points[], SkScalar tol);
}  // namespace GrPathUtils
}  // namespace pk

#endif
