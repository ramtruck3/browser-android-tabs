// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_UPDATER_UPDATER_CONSTANTS_H_
#define CHROME_UPDATER_UPDATER_CONSTANTS_H_

namespace updater {

// Command line switches.
//
// Crash the program for testing purposes.
extern const char kCrashMeSwitch[];

// Runs as the Crashpad handler.
extern const char kCrashHandlerSwitch[];

// Runs in test mode. Currently, it exits right away.
extern const char kTestSwitch[];

// Disables throttling for the crash reported until the following bug is fixed:
// https://bugs.chromium.org/p/crashpad/issues/detail?id=23
extern const char kNoRateLimit[];

// URLs.
//
// Omaha server end point.
extern const char kUpdaterJSONDefaultUrl[];

// The URL where crash reports are uploaded.
extern const char kCrashUploadURL[];
extern const char kCrashStagingUploadURL[];

}  // namespace updater

#endif  // CHROME_UPDATER_UPDATER_CONSTANTS_H_
