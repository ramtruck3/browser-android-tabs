// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/web_applications/system_web_app_ui_utils_chromeos.h"

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/web_applications/components/web_app_helpers.h"
#include "chrome/browser/web_applications/system_web_app_manager.h"
#include "chrome/browser/web_applications/web_app_provider.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "ui/base/mojo/window_open_disposition.mojom.h"
#include "ui/display/types/display_constants.h"

namespace web_app {

base::Optional<std::string> GetAppIdForSystemWebApp(Profile* profile,
                                                    SystemAppType app_type) {
  return WebAppProvider::Get(profile)
      ->system_web_app_manager()
      .GetAppIdForSystemApp(app_type);
}

Browser* LaunchSystemWebApp(Profile* profile,
                            SystemAppType app_type,
                            const GURL& url) {
  Browser* browser = FindSystemWebAppBrowser(profile, app_type);
  if (browser) {
    content::WebContents* web_contents =
        browser->tab_strip_model()->GetWebContentsAt(0);
    if (web_contents && web_contents->GetURL() == url) {
      browser->window()->Show();
      return browser;
    }
  }

  base::Optional<std::string> app_id =
      GetAppIdForSystemWebApp(profile, app_type);
  // TODO(calamity): Queue a task to launch app after it is installed.
  if (!app_id)
    return nullptr;

  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile)->GetExtensionById(
          app_id.value(), extensions::ExtensionRegistry::ENABLED);
  DCHECK(extension);

  // TODO(calamity): Plumb through better launch sources from callsites.
  AppLaunchParams params = CreateAppLaunchParamsWithEventFlags(
      profile, extension, 0, extensions::SOURCE_CHROME_INTERNAL,
      display::kInvalidDisplayId);
  params.override_url = url;

  if (!browser)
    browser = CreateApplicationWindow(params, url);

  ShowApplicationWindow(params, url, browser,
                        WindowOpenDisposition::CURRENT_TAB);
  return browser;
}

Browser* FindSystemWebAppBrowser(Profile* profile, SystemAppType app_type) {
  // TODO(calamity): Determine whether, during startup, we need to wait for
  // app install and then provide a valid answer here.
  base::Optional<std::string> app_id =
      GetAppIdForSystemWebApp(profile, app_type);
  if (!app_id)
    return nullptr;

  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile)->GetExtensionById(
          app_id.value(), extensions::ExtensionRegistry::ENABLED);
  DCHECK(extension);

  for (auto* browser : *BrowserList::GetInstance()) {
    if (browser->profile() != profile || !browser->is_app())
      continue;

    const extensions::Extension* browser_extension =
        extensions::ExtensionRegistry::Get(browser->profile())
            ->GetExtensionById(GetAppIdFromApplicationName(browser->app_name()),
                               extensions::ExtensionRegistry::EVERYTHING);
    if (browser_extension == extension)
      return browser;
  }

  return nullptr;
}

}  // namespace web_app
