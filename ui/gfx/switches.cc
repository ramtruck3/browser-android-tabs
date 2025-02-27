// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "ui/gfx/switches.h"

namespace switches {

// Force disables font subpixel positioning. This affects the character glyph
// sharpness, kerning, hinting and layout.
const char kDisableFontSubpixelPositioning[] =
    "disable-font-subpixel-positioning";

// Run in headless mode, i.e., without a UI or display server dependencies.
const char kHeadless[] = "headless";

}  // namespace switches

namespace features {

// Enables or disables the use of cc::PaintRecords as a backing store for
// ImageSkiaReps. This may reduce load on the UI thread by moving rasterization
// of drawables away from this thread.
const base::Feature kUsePaintRecordForImageSkia{
    "UsePaintRecordForImageSkia", base::FEATURE_ENABLED_BY_DEFAULT};

}  // namespace features
