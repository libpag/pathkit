/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

namespace pk {
/**
 *  Any of these can be specified by the build system (or SkUserConfig.h)
 *  to change the default values for a SkPaint. This file should not be
 *  edited directly.
 */

#ifndef PkPaintDefaults_MiterLimit
    #define PkPaintDefaults_MiterLimit PkIntToScalar(4)
#endif
}  // namespace pk
