// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/tabs/tabs_util.h"

#include "ash/public/cpp/window_properties.h"
#include "ash/public/interfaces/window_pin_type.mojom.h"
#include "base/metrics/histogram_macros.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/ui/ash/chrome_screenshot_grabber.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_window.h"
#include "content/public/browser/devtools_agent_host.h"
#include "ui/aura/window.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_types.h"
#include "ui/base/ui_base_features.h"

namespace extensions {
namespace tabs_util {

void SetLockedFullscreenState(Browser* browser, bool locked) {
  UMA_HISTOGRAM_BOOLEAN("Extensions.LockedFullscreenStateRequest", locked);

  // Disable ChromeVox before entering locked fullscreen.  Quickfix for
  // crbug.com/957950.
  auto* const accessibility_manager = chromeos::AccessibilityManager::Get();
  if (locked && accessibility_manager &&
      accessibility_manager->IsSpokenFeedbackEnabled()) {
    accessibility_manager->EnableSpokenFeedback(false);
  }

  aura::Window* window = browser->window()->GetNativeWindow();
  // TRUSTED_PINNED is used here because that one locks the window fullscreen
  // without allowing the user to exit (as opposed to regular PINNED).
  window->SetProperty(ash::kWindowPinTypeKey,
                      locked ? ash::mojom::WindowPinType::TRUSTED_PINNED
                             : ash::mojom::WindowPinType::NONE);

  // Update the set of available browser commands.
  browser->command_controller()->LockedFullscreenStateChanged();

  // Disallow screenshots in locked fullscreen mode.
  // TODO(isandrk, 816900): ChromeScreenshotGrabber isn't implemented in Mash
  // yet, remove this conditional when it becomes available.
  if (!features::IsMultiProcessMash())
    ChromeScreenshotGrabber::Get()->set_screenshots_allowed(!locked);

  // Reset the clipboard and kill dev tools when entering or exiting locked
  // fullscreen (security concerns).
  ui::Clipboard::GetForCurrentThread()->Clear(ui::CLIPBOARD_TYPE_COPY_PASTE);
  content::DevToolsAgentHost::DetachAllClients();

  // Disable ARC while in the locked fullscreen mode.
  arc::ArcSessionManager* const arc_session_manager =
      arc::ArcSessionManager::Get();
  Profile* const profile = browser->profile();
  if (arc_session_manager && arc::IsArcAllowedForProfile(profile)) {
    if (locked) {
      // Disable ARC, preserve data.
      arc_session_manager->RequestDisable();
    } else {
      // Re-enable ARC if needed.
      if (arc::IsArcPlayStoreEnabledForProfile(profile))
        arc_session_manager->RequestEnable();
    }
  }
}

}  // namespace tabs_util
}  // namespace extensions
