// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/autotest_private/autotest_private_api.h"

#include <set>
#include <sstream>
#include <utility>

#include "ash/public/cpp/ash_pref_names.h"
#include "ash/public/interfaces/ash_message_center_controller.mojom.h"
#include "ash/public/interfaces/constants.mojom.h"
#include "ash/shell.h"
#include "base/base64.h"
#include "base/bind.h"
#include "base/feature_list.h"
#include "base/lazy_instance.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/statistics_recorder.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/post_task.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/chromeos/assistant/assistant_util.h"
#include "chrome/browser/chromeos/crostini/crostini_export_import.h"
#include "chrome/browser/chromeos/crostini/crostini_manager.h"
#include "chrome/browser/chromeos/crostini/crostini_pref_names.h"
#include "chrome/browser/chromeos/crostini/crostini_registry_service.h"
#include "chrome/browser/chromeos/crostini/crostini_registry_service_factory.h"
#include "chrome/browser/chromeos/crostini/crostini_util.h"
#include "chrome/browser/chromeos/file_manager/path_util.h"
#include "chrome/browser/chromeos/login/lock/screen_locker.h"
#include "chrome/browser/chromeos/printing/cups_printers_manager.h"
#include "chrome/browser/chromeos/system/input_device_settings.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/shelf_spinner_controller.h"
#include "chrome/browser/ui/ash/login_screen_client.h"
#include "chrome/browser/ui/ash/tablet_mode_client.h"
#include "chrome/browser/ui/views/crostini/crostini_installer_view.h"
#include "chrome/browser/ui/views/crostini/crostini_uninstaller_view.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/extensions/api/autotest_private.h"
#include "chrome/common/pref_names.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/session_manager/session_manager_client.h"
#include "chromeos/printing/printer_configuration.h"
#include "chromeos/services/assistant/public/mojom/constants.mojom.h"
#include "chromeos/services/machine_learning/public/cpp/service_connection.h"
#include "components/arc/arc_prefs.h"
#include "components/arc/metrics/arc_metrics_constants.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/histogram_fetcher.h"
#include "content/public/common/service_manager_connection.h"
#include "extensions/browser/extension_function_registry.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/permissions/api_permission_set.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "net/base/filename_util.h"
#include "services/service_manager/public/cpp/connector.h"
#include "services/ws/public/mojom/constants.mojom.h"
#include "ui/base/ime/ime_bridge.h"
#include "ui/base/ui_base_features.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/message_center/public/cpp/notification.h"

namespace extensions {
namespace {

constexpr char kCrostiniNotAvailableForCurrentUserError[] =
    "Crostini is not available for the current user";

// Amount of time to give other processes to report their histograms.
constexpr base::TimeDelta kHistogramsRefreshTimeout =
    base::TimeDelta::FromSeconds(10);

int AccessArray(const volatile int arr[], const volatile int* index) {
  return arr[*index];
}

std::unique_ptr<base::ListValue> GetHostPermissions(const Extension* ext,
                                                    bool effective_perm) {
  const PermissionsData* permissions_data = ext->permissions_data();
  const URLPatternSet& pattern_set =
      effective_perm ? static_cast<const URLPatternSet&>(
                           permissions_data->GetEffectiveHostPermissions(
                               PermissionsData::EffectiveHostPermissionsMode::
                                   kIncludeTabSpecific))
                     : permissions_data->active_permissions().explicit_hosts();

  auto permissions = std::make_unique<base::ListValue>();
  for (URLPatternSet::const_iterator perm = pattern_set.begin();
       perm != pattern_set.end(); ++perm) {
    permissions->AppendString(perm->GetAsString());
  }

  return permissions;
}

std::unique_ptr<base::ListValue> GetAPIPermissions(const Extension* ext) {
  auto permissions = std::make_unique<base::ListValue>();
  std::set<std::string> perm_list =
      ext->permissions_data()->active_permissions().GetAPIsAsStrings();
  for (std::set<std::string>::const_iterator perm = perm_list.begin();
       perm != perm_list.end(); ++perm) {
    permissions->AppendString(*perm);
  }
  return permissions;
}

bool IsTestMode(content::BrowserContext* context) {
  return AutotestPrivateAPI::GetFactoryInstance()->Get(context)->test_mode();
}

std::string ConvertToString(message_center::NotificationType type) {
  switch (type) {
    case message_center::NOTIFICATION_TYPE_SIMPLE:
      return "simple";
    case message_center::NOTIFICATION_TYPE_BASE_FORMAT:
      return "base_format";
    case message_center::NOTIFICATION_TYPE_IMAGE:
      return "image";
    case message_center::NOTIFICATION_TYPE_MULTIPLE:
      return "multiple";
    case message_center::NOTIFICATION_TYPE_PROGRESS:
      return "progress";
    case message_center::NOTIFICATION_TYPE_CUSTOM:
      return "custom";
  }
  return "unknown";
}

std::unique_ptr<base::DictionaryValue> MakeDictionaryFromNotification(
    const message_center::Notification& notification) {
  auto result = std::make_unique<base::DictionaryValue>();
  result->SetString("id", notification.id());
  result->SetString("type", ConvertToString(notification.type()));
  result->SetString("title", notification.title());
  result->SetString("message", notification.message());
  result->SetInteger("priority", notification.priority());
  result->SetInteger("progress", notification.progress());
  return result;
}

std::string GetPrinterType(chromeos::CupsPrintersManager::PrinterClass type) {
  switch (type) {
    case chromeos::CupsPrintersManager::PrinterClass::kSaved:
      return "configured";
    case chromeos::CupsPrintersManager::PrinterClass::kEnterprise:
      return "enterprise";
    case chromeos::CupsPrintersManager::PrinterClass::kAutomatic:
      return "automatic";
    case chromeos::CupsPrintersManager::PrinterClass::kDiscovered:
      return "discovered";
    default:
      return "unknown";
  }
}

// Helper function to set whitelisted user pref based on |pref_name| with any
// specific pref validations. Returns error messages if any.
std::string SetWhitelistedPref(Profile* profile,
                               const std::string& pref_name,
                               const base::Value& value) {
  if (pref_name == arc::prefs::kVoiceInteractionEnabled ||
      pref_name == arc::prefs::kVoiceInteractionHotwordEnabled) {
    DCHECK(value.is_bool());
    ash::mojom::AssistantAllowedState allowed_state =
        assistant::IsAssistantAllowedForProfile(profile);
    if (allowed_state != ash::mojom::AssistantAllowedState::ALLOWED) {
      return base::StringPrintf("Assistant not allowed - state: %d",
                                allowed_state);
    }
  } else if (pref_name == ash::prefs::kAccessibilityVirtualKeyboardEnabled) {
    DCHECK(value.is_bool());
  } else if (pref_name == prefs::kLanguagePreloadEngines) {
    DCHECK(value.is_string());
  } else {
    return "The pref " + pref_name + "is not whitelisted.";
  }

  // Set value for the specified user pref after validation.
  profile->GetPrefs()->Set(pref_name, value);

  return std::string();
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateLogoutFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateLogoutFunction::~AutotestPrivateLogoutFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateLogoutFunction::Run() {
  DVLOG(1) << "AutotestPrivateLogoutFunction";
  if (!IsTestMode(browser_context()))
    chrome::AttemptUserExit();
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateRestartFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateRestartFunction::~AutotestPrivateRestartFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateRestartFunction::Run() {
  DVLOG(1) << "AutotestPrivateRestartFunction";
  if (!IsTestMode(browser_context()))
    chrome::AttemptRestart();
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateShutdownFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateShutdownFunction::~AutotestPrivateShutdownFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateShutdownFunction::Run() {
  std::unique_ptr<api::autotest_private::Shutdown::Params> params(
      api::autotest_private::Shutdown::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateShutdownFunction " << params->force;

  if (!IsTestMode(browser_context()))
    chrome::AttemptExit();
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateLoginStatusFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateLoginStatusFunction::~AutotestPrivateLoginStatusFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateLoginStatusFunction::Run() {
  DVLOG(1) << "AutotestPrivateLoginStatusFunction";

  LoginScreenClient::Get()->login_screen()->IsReadyForPassword(base::BindOnce(
      &AutotestPrivateLoginStatusFunction::OnIsReadyForPassword, this));
  return RespondLater();
}

void AutotestPrivateLoginStatusFunction::OnIsReadyForPassword(bool is_ready) {
  auto result = std::make_unique<base::DictionaryValue>();
  const user_manager::UserManager* user_manager =
      user_manager::UserManager::Get();

  // default_screen_locker()->locked() is set when the UI is ready, so this
  // tells us both views based lockscreen UI and screenlocker are ready.
  const bool is_screen_locked =
      !!chromeos::ScreenLocker::default_screen_locker() &&
      chromeos::ScreenLocker::default_screen_locker()->locked();

  if (user_manager) {
    result->SetBoolean("isLoggedIn", user_manager->IsUserLoggedIn());
    result->SetBoolean("isOwner", user_manager->IsCurrentUserOwner());
    result->SetBoolean("isScreenLocked", is_screen_locked);
    result->SetBoolean("isReadyForPassword", is_ready);
    if (user_manager->IsUserLoggedIn()) {
      result->SetBoolean("isRegularUser",
                         user_manager->IsLoggedInAsUserWithGaiaAccount());
      result->SetBoolean("isGuest", user_manager->IsLoggedInAsGuest());
      result->SetBoolean("isKiosk", user_manager->IsLoggedInAsKioskApp());

      const user_manager::User* user = user_manager->GetActiveUser();
      result->SetString("email", user->GetAccountId().GetUserEmail());
      result->SetString("displayEmail", user->display_email());

      std::string user_image;
      switch (user->image_index()) {
        case user_manager::User::USER_IMAGE_EXTERNAL:
          user_image = "file";
          break;

        case user_manager::User::USER_IMAGE_PROFILE:
          user_image = "profile";
          break;

        default:
          user_image = base::NumberToString(user->image_index());
          break;
      }
      result->SetString("userImage", user_image);
    }
  }
  Respond(OneArgument(std::move(result)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateLockScreenFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateLockScreenFunction::~AutotestPrivateLockScreenFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateLockScreenFunction::Run() {
  DVLOG(1) << "AutotestPrivateLockScreenFunction";

  chromeos::SessionManagerClient::Get()->RequestLockScreen();
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetExtensionsInfoFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetExtensionsInfoFunction::
    ~AutotestPrivateGetExtensionsInfoFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateGetExtensionsInfoFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetExtensionsInfoFunction";

  ExtensionService* service =
      ExtensionSystem::Get(browser_context())->extension_service();
  ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context());
  const ExtensionSet& extensions = registry->enabled_extensions();
  const ExtensionSet& disabled_extensions = registry->disabled_extensions();
  ExtensionActionManager* extension_action_manager =
      ExtensionActionManager::Get(browser_context());

  auto extensions_values = std::make_unique<base::ListValue>();
  ExtensionList all;
  all.insert(all.end(), extensions.begin(), extensions.end());
  all.insert(all.end(), disabled_extensions.begin(), disabled_extensions.end());
  for (ExtensionList::const_iterator it = all.begin(); it != all.end(); ++it) {
    const Extension* extension = it->get();
    std::string id = extension->id();
    std::unique_ptr<base::DictionaryValue> extension_value(
        new base::DictionaryValue);
    extension_value->SetString("id", id);
    extension_value->SetString("version", extension->VersionString());
    extension_value->SetString("name", extension->name());
    extension_value->SetString("publicKey", extension->public_key());
    extension_value->SetString("description", extension->description());
    extension_value->SetString(
        "backgroundUrl", BackgroundInfo::GetBackgroundURL(extension).spec());
    extension_value->SetString(
        "optionsUrl", OptionsPageInfo::GetOptionsPage(extension).spec());

    extension_value->Set("hostPermissions",
                         GetHostPermissions(extension, false));
    extension_value->Set("effectiveHostPermissions",
                         GetHostPermissions(extension, true));
    extension_value->Set("apiPermissions", GetAPIPermissions(extension));

    Manifest::Location location = extension->location();
    extension_value->SetBoolean("isComponent", location == Manifest::COMPONENT);
    extension_value->SetBoolean("isInternal", location == Manifest::INTERNAL);
    extension_value->SetBoolean("isUserInstalled",
                                location == Manifest::INTERNAL ||
                                    Manifest::IsUnpackedLocation(location));
    extension_value->SetBoolean("isEnabled", service->IsExtensionEnabled(id));
    extension_value->SetBoolean(
        "allowedInIncognito", util::IsIncognitoEnabled(id, browser_context()));
    extension_value->SetBoolean(
        "hasPageAction",
        extension_action_manager->GetPageAction(*extension) != NULL);

    extensions_values->Append(std::move(extension_value));
  }

  std::unique_ptr<base::DictionaryValue> return_value(
      new base::DictionaryValue);
  return_value->Set("extensions", std::move(extensions_values));
  return RespondNow(OneArgument(std::move(return_value)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSimulateAsanMemoryBugFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSimulateAsanMemoryBugFunction::
    ~AutotestPrivateSimulateAsanMemoryBugFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSimulateAsanMemoryBugFunction::Run() {
  DVLOG(1) << "AutotestPrivateSimulateAsanMemoryBugFunction";

  if (!IsTestMode(browser_context())) {
    // This array is volatile not to let compiler optimize us out.
    volatile int testarray[3] = {0, 0, 0};

    // Cause Address Sanitizer to abort this process.
    volatile int index = 5;
    AccessArray(testarray, &index);
  }
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetTouchpadSensitivityFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetTouchpadSensitivityFunction::
    ~AutotestPrivateSetTouchpadSensitivityFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetTouchpadSensitivityFunction::Run() {
  std::unique_ptr<api::autotest_private::SetTouchpadSensitivity::Params> params(
      api::autotest_private::SetTouchpadSensitivity::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetTouchpadSensitivityFunction " << params->value;

  chromeos::system::InputDeviceSettings::Get()->SetTouchpadSensitivity(
      params->value);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetTapToClickFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetTapToClickFunction::~AutotestPrivateSetTapToClickFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateSetTapToClickFunction::Run() {
  std::unique_ptr<api::autotest_private::SetTapToClick::Params> params(
      api::autotest_private::SetTapToClick::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetTapToClickFunction " << params->enabled;

  chromeos::system::InputDeviceSettings::Get()->SetTapToClick(params->enabled);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetThreeFingerClickFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetThreeFingerClickFunction::
    ~AutotestPrivateSetThreeFingerClickFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetThreeFingerClickFunction::Run() {
  std::unique_ptr<api::autotest_private::SetThreeFingerClick::Params> params(
      api::autotest_private::SetThreeFingerClick::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetThreeFingerClickFunction " << params->enabled;

  chromeos::system::InputDeviceSettings::Get()->SetThreeFingerClick(
      params->enabled);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetTapDraggingFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetTapDraggingFunction::
    ~AutotestPrivateSetTapDraggingFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateSetTapDraggingFunction::Run() {
  std::unique_ptr<api::autotest_private::SetTapDragging::Params> params(
      api::autotest_private::SetTapDragging::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetTapDraggingFunction " << params->enabled;

  chromeos::system::InputDeviceSettings::Get()->SetTapDragging(params->enabled);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetNaturalScrollFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetNaturalScrollFunction::
    ~AutotestPrivateSetNaturalScrollFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetNaturalScrollFunction::Run() {
  std::unique_ptr<api::autotest_private::SetNaturalScroll::Params> params(
      api::autotest_private::SetNaturalScroll::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetNaturalScrollFunction " << params->enabled;

  chromeos::system::InputDeviceSettings::Get()->SetNaturalScroll(
      params->enabled);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetMouseSensitivityFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetMouseSensitivityFunction::
    ~AutotestPrivateSetMouseSensitivityFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetMouseSensitivityFunction::Run() {
  std::unique_ptr<api::autotest_private::SetMouseSensitivity::Params> params(
      api::autotest_private::SetMouseSensitivity::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetMouseSensitivityFunction " << params->value;

  chromeos::system::InputDeviceSettings::Get()->SetMouseSensitivity(
      params->value);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetPrimaryButtonRightFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetPrimaryButtonRightFunction::
    ~AutotestPrivateSetPrimaryButtonRightFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetPrimaryButtonRightFunction::Run() {
  std::unique_ptr<api::autotest_private::SetPrimaryButtonRight::Params> params(
      api::autotest_private::SetPrimaryButtonRight::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetPrimaryButtonRightFunction " << params->right;

  chromeos::system::InputDeviceSettings::Get()->SetPrimaryButtonRight(
      params->right);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetMouseReverseScrollFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetMouseReverseScrollFunction::
    ~AutotestPrivateSetMouseReverseScrollFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetMouseReverseScrollFunction::Run() {
  std::unique_ptr<api::autotest_private::SetMouseReverseScroll::Params> params(
      api::autotest_private::SetMouseReverseScroll::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  DVLOG(1) << "AutotestPrivateSetMouseReverseScrollFunction "
           << params->enabled;

  chromeos::system::InputDeviceSettings::Get()->SetMouseReverseScroll(
      params->enabled);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetVisibleNotificationsFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetVisibleNotificationsFunction::
    AutotestPrivateGetVisibleNotificationsFunction() = default;
AutotestPrivateGetVisibleNotificationsFunction::
    ~AutotestPrivateGetVisibleNotificationsFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateGetVisibleNotificationsFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetVisibleNotificationsFunction";

  auto* connection = content::ServiceManagerConnection::GetForProcess();
  connection->GetConnector()->BindInterface(ash::mojom::kServiceName,
                                            &controller_);
  controller_->GetActiveNotifications(base::BindOnce(
      &AutotestPrivateGetVisibleNotificationsFunction::OnGotNotifications,
      this));
  return RespondLater();
}

void AutotestPrivateGetVisibleNotificationsFunction::OnGotNotifications(
    const std::vector<message_center::Notification>& notifications) {
  auto values = std::make_unique<base::ListValue>();
  for (const auto& notification : notifications) {
    values->Append(MakeDictionaryFromNotification(notification));
  }
  Respond(OneArgument(std::move(values)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetArcStateFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetArcStateFunction::~AutotestPrivateGetArcStateFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateGetArcStateFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetArcStateFunction";

  api::autotest_private::ArcState arc_state;
  Profile* const profile = Profile::FromBrowserContext(browser_context());

  if (!arc::IsArcAllowedForProfile(profile))
    return RespondNow(Error("ARC is not available for the current user"));

  arc_state.provisioned = arc::IsArcProvisioned(profile);
  arc_state.tos_needed = arc::IsArcTermsOfServiceNegotiationNeeded(profile);
  return RespondNow(OneArgument(arc_state.ToValue()));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetPlayStoreStateFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetPlayStoreStateFunction::
    ~AutotestPrivateGetPlayStoreStateFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateGetPlayStoreStateFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetPlayStoreStateFunction";

  api::autotest_private::PlayStoreState play_store_state;
  play_store_state.allowed = false;
  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (arc::IsArcAllowedForProfile(profile)) {
    play_store_state.allowed = true;
    play_store_state.enabled =
        std::make_unique<bool>(arc::IsArcPlayStoreEnabledForProfile(profile));
    play_store_state.managed = std::make_unique<bool>(
        arc::IsArcPlayStoreEnabledPreferenceManagedForProfile(profile));
  }
  return RespondNow(OneArgument(play_store_state.ToValue()));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetPlayStoreEnabledFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetPlayStoreEnabledFunction::
    ~AutotestPrivateSetPlayStoreEnabledFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetPlayStoreEnabledFunction::Run() {
  std::unique_ptr<api::autotest_private::SetPlayStoreEnabled::Params> params(
      api::autotest_private::SetPlayStoreEnabled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateSetPlayStoreEnabledFunction " << params->enabled;

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (arc::IsArcAllowedForProfile(profile)) {
    if (!arc::SetArcPlayStoreEnabledForProfile(profile, params->enabled)) {
      return RespondNow(
          Error("ARC enabled state cannot be changed for the current user"));
    }
    // kArcLocationServiceEnabled and kArcBackupRestoreEnabled are prefs that
    // set together with enabling ARC. That is why we set it here not using
    // SetWhitelistedPref. At this moment, we don't distinguish the actual
    // values and set kArcLocationServiceEnabled to true and leave
    // kArcBackupRestoreEnabled unmodified, which is acceptable for autotests
    // currently.
    profile->GetPrefs()->SetBoolean(arc::prefs::kArcLocationServiceEnabled,
                                    true);
    return RespondNow(NoArguments());
  } else {
    return RespondNow(Error("ARC is not available for the current user"));
  }
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetHistogramFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetHistogramFunction::~AutotestPrivateGetHistogramFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateGetHistogramFunction::Run() {
  std::unique_ptr<api::autotest_private::GetHistogram::Params> params(
      api::autotest_private::GetHistogram::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateGetHistogramFunction " << params->name;

  // Collect histogram data from other processes before responding. Otherwise,
  // we'd report stale data for histograms that are e.g. recorded by renderers.
  content::FetchHistogramsAsynchronously(
      base::ThreadTaskRunnerHandle::Get(),
      base::BindRepeating(
          &AutotestPrivateGetHistogramFunction::RespondOnHistogramsFetched,
          this, params->name),
      kHistogramsRefreshTimeout);
  return RespondLater();
}

void AutotestPrivateGetHistogramFunction::RespondOnHistogramsFetched(
    const std::string& name) {
  // Incorporate the data collected by content::FetchHistogramsAsynchronously().
  base::StatisticsRecorder::ImportProvidedHistograms();
  Respond(GetHistogram(name));
}

ExtensionFunction::ResponseValue
AutotestPrivateGetHistogramFunction::GetHistogram(const std::string& name) {
  const base::HistogramBase* histogram =
      base::StatisticsRecorder::FindHistogram(name);
  if (!histogram)
    return Error(base::StrCat({"Histogram ", name, " not found"}));

  std::unique_ptr<base::HistogramSamples> samples =
      histogram->SnapshotSamples();
  api::autotest_private::Histogram result;

  for (std::unique_ptr<base::SampleCountIterator> it = samples->Iterator();
       !it->Done(); it->Next()) {
    base::HistogramBase::Sample min = 0;
    int64_t max = 0;
    base::HistogramBase::Count count = 0;
    it->Get(&min, &max, &count);

    api::autotest_private::HistogramBucket bucket;
    bucket.min = min;
    bucket.max = max;
    bucket.count = count;
    result.buckets.push_back(std::move(bucket));
  }

  return OneArgument(result.ToValue());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateIsAppShownFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateIsAppShownFunction::~AutotestPrivateIsAppShownFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateIsAppShownFunction::Run() {
  std::unique_ptr<api::autotest_private::IsAppShown::Params> params(
      api::autotest_private::IsAppShown::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateIsAppShownFunction " << params->app_id;

  ChromeLauncherController* const controller =
      ChromeLauncherController::instance();
  if (!controller)
    return RespondNow(Error("Controller not available"));

  const ash::ShelfItem* item =
      controller->GetItem(ash::ShelfID(params->app_id));
  // App must be running and not pending in deferred launch.
  const bool window_attached =
      item && item->status == ash::ShelfItemStatus::STATUS_RUNNING &&
      !controller->GetShelfSpinnerController()->HasApp(params->app_id);
  return RespondNow(
      OneArgument(std::make_unique<base::Value>(window_attached)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateIsAppShownFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateIsArcProvisionedFunction::
    ~AutotestPrivateIsArcProvisionedFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateIsArcProvisionedFunction::Run() {
  DVLOG(1) << "AutotestPrivateIsArcProvisionedFunction";
  return RespondNow(OneArgument(std::make_unique<base::Value>(
      arc::IsArcProvisioned(Profile::FromBrowserContext(browser_context())))));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetArcPackageFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetArcAppFunction::~AutotestPrivateGetArcAppFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateGetArcAppFunction::Run() {
  std::unique_ptr<api::autotest_private::GetArcApp::Params> params(
      api::autotest_private::GetArcApp::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateGetArcAppFunction " << params->app_id;

  ArcAppListPrefs* const prefs =
      ArcAppListPrefs::Get(Profile::FromBrowserContext(browser_context()));
  if (!prefs)
    return RespondNow(Error("ARC is not available"));

  const std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
      prefs->GetApp(params->app_id);
  if (!app_info)
    return RespondNow(Error("App is not available"));

  auto app_value = std::make_unique<base::DictionaryValue>();

  app_value->SetKey("name", base::Value(app_info->name));
  app_value->SetKey("packageName", base::Value(app_info->package_name));
  app_value->SetKey("activity", base::Value(app_info->activity));
  app_value->SetKey("intentUri", base::Value(app_info->intent_uri));
  app_value->SetKey("iconResourceId", base::Value(app_info->icon_resource_id));
  app_value->SetKey("lastLaunchTime",
                    base::Value(app_info->last_launch_time.ToJsTime()));
  app_value->SetKey("installTime",
                    base::Value(app_info->install_time.ToJsTime()));
  app_value->SetKey("sticky", base::Value(app_info->sticky));
  app_value->SetKey("notificationsEnabled",
                    base::Value(app_info->notifications_enabled));
  app_value->SetKey("ready", base::Value(app_info->ready));
  app_value->SetKey("suspended", base::Value(app_info->suspended));
  app_value->SetKey("showInLauncher", base::Value(app_info->show_in_launcher));
  app_value->SetKey("shortcut", base::Value(app_info->shortcut));
  app_value->SetKey("launchable", base::Value(app_info->launchable));

  return RespondNow(OneArgument(std::move(app_value)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetArcPackageFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetArcPackageFunction::~AutotestPrivateGetArcPackageFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateGetArcPackageFunction::Run() {
  std::unique_ptr<api::autotest_private::GetArcPackage::Params> params(
      api::autotest_private::GetArcPackage::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateGetArcPackageFunction " << params->package_name;

  ArcAppListPrefs* const prefs =
      ArcAppListPrefs::Get(Profile::FromBrowserContext(browser_context()));
  if (!prefs)
    return RespondNow(Error("ARC is not available"));

  const std::unique_ptr<ArcAppListPrefs::PackageInfo> package_info =
      prefs->GetPackage(params->package_name);
  if (!package_info)
    return RespondNow(Error("Package is not available"));

  auto package_value = std::make_unique<base::DictionaryValue>();
  package_value->SetKey("packageName", base::Value(package_info->package_name));
  package_value->SetKey("packageVersion",
                        base::Value(package_info->package_version));
  package_value->SetKey(
      "lastBackupAndroidId",
      base::Value(base::NumberToString(package_info->last_backup_android_id)));
  package_value->SetKey("lastBackupTime",
                        base::Value(base::Time::FromDeltaSinceWindowsEpoch(
                                        base::TimeDelta::FromMicroseconds(
                                            package_info->last_backup_time))
                                        .ToJsTime()));
  package_value->SetKey("shouldSync", base::Value(package_info->should_sync));
  package_value->SetKey("system", base::Value(package_info->system));
  package_value->SetKey("vpnProvider", base::Value(package_info->vpn_provider));
  return RespondNow(OneArgument(std::move(package_value)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateLaunchArcIntentFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateLaunchArcAppFunction::~AutotestPrivateLaunchArcAppFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateLaunchArcAppFunction::Run() {
  std::unique_ptr<api::autotest_private::LaunchArcApp::Params> params(
      api::autotest_private::LaunchArcApp::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateLaunchArcIntentFunction " << params->app_id << "/"
           << params->intent;

  base::Optional<std::string> launch_intent;
  if (!params->intent.empty())
    launch_intent = params->intent;
  const bool result = arc::LaunchAppWithIntent(
      Profile::FromBrowserContext(browser_context()), params->app_id,
      launch_intent, 0 /* event_flags */,
      arc::UserInteractionType::APP_STARTED_FROM_EXTENSION_API,
      0 /* display_id */);
  return RespondNow(OneArgument(std::make_unique<base::Value>(result)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateLaunchAppFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateLaunchAppFunction::~AutotestPrivateLaunchAppFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateLaunchAppFunction::Run() {
  std::unique_ptr<api::autotest_private::LaunchApp::Params> params(
      api::autotest_private::LaunchApp::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateLaunchAppFunction " << params->app_id;

  ChromeLauncherController* const controller =
      ChromeLauncherController::instance();
  if (!controller)
    return RespondNow(Error("Controller not available"));
  controller->LaunchApp(ash::ShelfID(params->app_id),
                        ash::ShelfLaunchSource::LAUNCH_FROM_UNKNOWN,
                        0, /* event_flags */
                        display::Screen::GetScreen()->GetPrimaryDisplay().id());
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateLaunchAppFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateCloseAppFunction::~AutotestPrivateCloseAppFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateCloseAppFunction::Run() {
  std::unique_ptr<api::autotest_private::CloseApp::Params> params(
      api::autotest_private::CloseApp::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateCloseAppFunction " << params->app_id;

  ChromeLauncherController* const controller =
      ChromeLauncherController::instance();
  if (!controller)
    return RespondNow(Error("Controller not available"));
  controller->Close(ash::ShelfID(params->app_id));
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetCrostiniEnabledFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetCrostiniEnabledFunction::
    ~AutotestPrivateSetCrostiniEnabledFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetCrostiniEnabledFunction::Run() {
  std::unique_ptr<api::autotest_private::SetCrostiniEnabled::Params> params(
      api::autotest_private::SetCrostiniEnabled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateSetCrostiniEnabledFunction " << params->enabled;

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (!crostini::IsCrostiniUIAllowedForProfile(profile))
    return RespondNow(Error(kCrostiniNotAvailableForCurrentUserError));

  // Set the preference to indicate Crostini is enabled/disabled.
  profile->GetPrefs()->SetBoolean(crostini::prefs::kCrostiniEnabled,
                                  params->enabled);
  // Set the flag to indicate we are in testing mode so that Chrome doesn't
  // try to start the VM/container itself.
  crostini::CrostiniManager* crostini_manager =
      crostini::CrostiniManager::GetForProfile(profile);
  crostini_manager->set_skip_restart_for_testing();
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateRunCrostiniInstallerFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateRunCrostiniInstallerFunction::
    ~AutotestPrivateRunCrostiniInstallerFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateRunCrostiniInstallerFunction::Run() {
  DVLOG(1) << "AutotestPrivateInstallCrostiniFunction";

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (!crostini::IsCrostiniUIAllowedForProfile(profile))
    return RespondNow(Error(kCrostiniNotAvailableForCurrentUserError));

  // Run GUI installer which will install crostini vm / container and
  // start terminal app on completion.  After starting the installer,
  // we call RestartCrostini and we will be put in the pending restarters
  // queue and be notified on success/otherwise of installation.
  CrostiniInstallerView::Show(profile);
  CrostiniInstallerView::GetActiveViewForTesting()->Accept();
  crostini::CrostiniManager::GetForProfile(profile)->RestartCrostini(
      crostini::kCrostiniDefaultVmName, crostini::kCrostiniDefaultContainerName,
      base::BindOnce(
          &AutotestPrivateRunCrostiniInstallerFunction::CrostiniRestarted,
          this));

  return RespondLater();
}

void AutotestPrivateRunCrostiniInstallerFunction::CrostiniRestarted(
    crostini::CrostiniResult result) {
  if (result == crostini::CrostiniResult::SUCCESS) {
    Respond(NoArguments());
  } else {
    Respond(Error("Error installing crostini"));
  }
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateRunCrostiniUninstallerFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateRunCrostiniUninstallerFunction::
    ~AutotestPrivateRunCrostiniUninstallerFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateRunCrostiniUninstallerFunction::Run() {
  DVLOG(1) << "AutotestPrivateRunCrostiniUninstallerFunction";

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (!crostini::IsCrostiniUIAllowedForProfile(profile))
    return RespondNow(Error(kCrostiniNotAvailableForCurrentUserError));

  // Run GUI uninstaller which will remove crostini vm / container. We then
  // receive the callback with the result when that is complete.
  crostini::CrostiniManager::GetForProfile(profile)->AddRemoveCrostiniCallback(
      base::BindOnce(
          &AutotestPrivateRunCrostiniUninstallerFunction::CrostiniRemoved,
          this));
  CrostiniUninstallerView::Show(profile);
  CrostiniUninstallerView::GetActiveViewForTesting()->Accept();
  return RespondLater();
}

void AutotestPrivateRunCrostiniUninstallerFunction::CrostiniRemoved(
    crostini::CrostiniResult result) {
  if (result == crostini::CrostiniResult::SUCCESS)
    Respond(NoArguments());
  else
    Respond(Error("Error uninstalling crostini"));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateExportCrostiniFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateExportCrostiniFunction::
    ~AutotestPrivateExportCrostiniFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateExportCrostiniFunction::Run() {
  std::unique_ptr<api::autotest_private::ExportCrostini::Params> params(
      api::autotest_private::ExportCrostini::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateExportCrostiniFunction " << params->path;

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (!crostini::IsCrostiniUIAllowedForProfile(profile)) {
    return RespondNow(Error(kCrostiniNotAvailableForCurrentUserError));
  }

  base::FilePath path(params->path);
  if (path.ReferencesParent()) {
    return RespondNow(Error("Invalid export path must not reference parent"));
  }

  crostini::CrostiniExportImport::GetForProfile(profile)->ExportContainer(
      crostini::ContainerId(crostini::kCrostiniDefaultVmName,
                            crostini::kCrostiniDefaultContainerName),
      file_manager::util::GetDownloadsFolderForProfile(profile).Append(path),
      base::BindOnce(&AutotestPrivateExportCrostiniFunction::CrostiniExported,
                     this));

  return RespondLater();
}

void AutotestPrivateExportCrostiniFunction::CrostiniExported(
    crostini::CrostiniResult result) {
  if (result == crostini::CrostiniResult::SUCCESS) {
    Respond(NoArguments());
  } else {
    Respond(Error("Error exporting crostini"));
  }
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateImportCrostiniFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateImportCrostiniFunction::
    ~AutotestPrivateImportCrostiniFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateImportCrostiniFunction::Run() {
  std::unique_ptr<api::autotest_private::ImportCrostini::Params> params(
      api::autotest_private::ImportCrostini::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateImportCrostiniFunction " << params->path;

  Profile* profile = Profile::FromBrowserContext(browser_context());
  if (!crostini::IsCrostiniUIAllowedForProfile(profile))
    return RespondNow(Error(kCrostiniNotAvailableForCurrentUserError));

  base::FilePath path(params->path);
  if (path.ReferencesParent()) {
    return RespondNow(Error("Invalid import path must not reference parent"));
  }
  crostini::CrostiniExportImport::GetForProfile(profile)->ImportContainer(
      crostini::ContainerId(crostini::kCrostiniDefaultVmName,
                            crostini::kCrostiniDefaultContainerName),
      file_manager::util::GetDownloadsFolderForProfile(profile).Append(path),
      base::BindOnce(&AutotestPrivateImportCrostiniFunction::CrostiniImported,
                     this));

  return RespondLater();
}

void AutotestPrivateImportCrostiniFunction::CrostiniImported(
    crostini::CrostiniResult result) {
  if (result == crostini::CrostiniResult::SUCCESS) {
    Respond(NoArguments());
  } else {
    Respond(Error("Error importing crostini"));
  }
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateTakeScreenshotFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateTakeScreenshotFunction::
    ~AutotestPrivateTakeScreenshotFunction() = default;

ExtensionFunction::ResponseAction AutotestPrivateTakeScreenshotFunction::Run() {
  DVLOG(1) << "AutotestPrivateTakeScreenshotFunction";
  auto grabber = std::make_unique<ui::ScreenshotGrabber>();
  // TODO(mash): Fix for mash, http://crbug.com/557397
  aura::Window* primary_root = ash::Shell::GetPrimaryRootWindow();
  // Pass the ScreenshotGrabber to the callback so that it stays alive for the
  // duration of the operation, it'll then get deallocated when the callback
  // completes.
  grabber->TakeScreenshot(
      primary_root, primary_root->bounds(),
      base::BindOnce(&AutotestPrivateTakeScreenshotFunction::ScreenshotTaken,
                     this, base::Passed(&grabber)));
  return RespondLater();
}

void AutotestPrivateTakeScreenshotFunction::ScreenshotTaken(
    std::unique_ptr<ui::ScreenshotGrabber> grabber,
    ui::ScreenshotResult screenshot_result,
    scoped_refptr<base::RefCountedMemory> png_data) {
  if (screenshot_result == ui::ScreenshotResult::SUCCESS) {
    // Base64 encode the result so we can return it as a string.
    std::string base64Png(png_data->front(),
                          png_data->front() + png_data->size());
    base::Base64Encode(base64Png, &base64Png);
    Respond(OneArgument(std::make_unique<base::Value>(base64Png)));
  } else {
    Respond(Error(base::StrCat(
        {"Error taking screenshot ",
         base::NumberToString(static_cast<int>(screenshot_result))})));
  }
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetPrinterListFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetPrinterListFunction::AutotestPrivateGetPrinterListFunction()
    : results_(std::make_unique<base::Value>(base::Value::Type::LIST)) {}

AutotestPrivateGetPrinterListFunction::
    ~AutotestPrivateGetPrinterListFunction() {
  printers_manager_->RemoveObserver(this);
}

ExtensionFunction::ResponseAction AutotestPrivateGetPrinterListFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetPrinterListFunction";

  Profile* profile = Profile::FromBrowserContext(browser_context());
  printers_manager_ = chromeos::CupsPrintersManager::Create(profile);
  printers_manager_->AddObserver(this);

  // Set up a timer to finish waiting after 10 seconds
  timeout_timer_.Start(
      FROM_HERE, base::TimeDelta::FromSeconds(10),
      base::BindOnce(
          &AutotestPrivateGetPrinterListFunction::RespondWithTimeoutError,
          this));

  return RespondLater();
}

void AutotestPrivateGetPrinterListFunction::RespondWithTimeoutError() {
  if (did_respond())
    return;
  Respond(Error("Timeout occured before Enterprise printers were initialized"));
}

void AutotestPrivateGetPrinterListFunction::RespondWithSuccess() {
  if (did_respond())
    return;
  Respond(OneArgument(std::move(results_)));
  timeout_timer_.AbandonAndStop();
}

void AutotestPrivateGetPrinterListFunction::OnEnterprisePrintersInitialized() {
  // We are ready to get the list of printers and finish.
  std::vector<chromeos::CupsPrintersManager::PrinterClass> printer_type = {
      chromeos::CupsPrintersManager::PrinterClass::kSaved,
      chromeos::CupsPrintersManager::PrinterClass::kEnterprise,
      chromeos::CupsPrintersManager::PrinterClass::kAutomatic};
  base::Value::ListStorage& vresults = results_->GetList();
  for (const auto& type : printer_type) {
    std::vector<chromeos::Printer> printer_list =
        printers_manager_->GetPrinters(type);
    for (const auto& printer : printer_list) {
      vresults.push_back(base::Value(base::Value::Type::DICTIONARY));
      base::Value& result = vresults.back();
      result.SetKey("printerName", base::Value(printer.display_name()));
      result.SetKey("printerId", base::Value(printer.id()));
      result.SetKey("printerType", base::Value(GetPrinterType(type)));
    }
  }
  // We have to respond in separate task, because it will cause a destruction of
  // CupsPrintersManager
  PostTask(
      FROM_HERE,
      base::BindOnce(&AutotestPrivateGetPrinterListFunction::RespondWithSuccess,
                     this));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateUpdatePrinterFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateUpdatePrinterFunction::~AutotestPrivateUpdatePrinterFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateUpdatePrinterFunction::Run() {
  std::unique_ptr<api::autotest_private::UpdatePrinter::Params> params(
      api::autotest_private::UpdatePrinter::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateUpdatePrinterFunction";

  const api::autotest_private::Printer& js_printer = params->printer;
  chromeos::Printer printer(js_printer.printer_id ? *js_printer.printer_id
                                                  : "");
  printer.set_display_name(js_printer.printer_name);
  if (js_printer.printer_desc)
    printer.set_description(*js_printer.printer_desc);

  if (js_printer.printer_make_and_model)
    printer.set_make_and_model(*js_printer.printer_make_and_model);

  if (js_printer.printer_uri)
    printer.set_uri(*js_printer.printer_uri);

  if (js_printer.printer_ppd) {
    const GURL ppd =
        net::FilePathToFileURL(base::FilePath(*js_printer.printer_ppd));
    if (ppd.is_valid())
      printer.mutable_ppd_reference()->user_supplied_ppd_url = ppd.spec();
    else
      LOG(ERROR) << "Invalid ppd path: " << *js_printer.printer_ppd;
  }
  auto printers_manager = chromeos::CupsPrintersManager::Create(
      Profile::FromBrowserContext(browser_context()));
  printers_manager->UpdateSavedPrinter(printer);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateRemovePrinterFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateRemovePrinterFunction::~AutotestPrivateRemovePrinterFunction() =
    default;

ExtensionFunction::ResponseAction AutotestPrivateRemovePrinterFunction::Run() {
  std::unique_ptr<api::autotest_private::RemovePrinter::Params> params(
      api::autotest_private::RemovePrinter::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateRemovePrinterFunction " << params->printer_id;

  auto printers_manager = chromeos::CupsPrintersManager::Create(
      Profile::FromBrowserContext(browser_context()));
  printers_manager->RemoveSavedPrinter(params->printer_id);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateBootstrapMachineLearningServiceFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateBootstrapMachineLearningServiceFunction::
    AutotestPrivateBootstrapMachineLearningServiceFunction() = default;
AutotestPrivateBootstrapMachineLearningServiceFunction::
    ~AutotestPrivateBootstrapMachineLearningServiceFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateBootstrapMachineLearningServiceFunction::Run() {
  DVLOG(1) << "AutotestPrivateBootstrapMachineLearningServiceFunction";

  // Load a model. This will first bootstrap the Mojo connection to ML Service.
  chromeos::machine_learning::ServiceConnection::GetInstance()->LoadModel(
      chromeos::machine_learning::mojom::ModelSpec::New(
          chromeos::machine_learning::mojom::ModelId::TEST_MODEL),
      mojo::MakeRequest(&model_),
      base::BindOnce(
          &AutotestPrivateBootstrapMachineLearningServiceFunction::ModelLoaded,
          this));
  model_.set_connection_error_handler(base::BindOnce(
      &AutotestPrivateBootstrapMachineLearningServiceFunction::ConnectionError,
      this));
  return RespondLater();
}

void AutotestPrivateBootstrapMachineLearningServiceFunction::ModelLoaded(
    chromeos::machine_learning::mojom::LoadModelResult result) {
  if (result == chromeos::machine_learning::mojom::LoadModelResult::OK) {
    Respond(NoArguments());
  } else {
    Respond(Error(base::StrCat(
        {"Model load error ", (std::ostringstream() << result).str()})));
  }
}

void AutotestPrivateBootstrapMachineLearningServiceFunction::ConnectionError() {
  Respond(Error("ML Service connection error"));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetAssistantEnabled
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetAssistantEnabledFunction::
    AutotestPrivateSetAssistantEnabledFunction() {
  auto* connection = content::ServiceManagerConnection::GetForProcess();
  assistant_state_.Init(connection->GetConnector());
  assistant_state_.AddObserver(this);
}

AutotestPrivateSetAssistantEnabledFunction::
    ~AutotestPrivateSetAssistantEnabledFunction() {
  assistant_state_.RemoveObserver(this);
}

ExtensionFunction::ResponseAction
AutotestPrivateSetAssistantEnabledFunction::Run() {
  DVLOG(1) << "AutotestPrivateSetAssistantEnabledFunction";

  std::unique_ptr<api::autotest_private::SetAssistantEnabled::Params> params(
      api::autotest_private::SetAssistantEnabled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  Profile* profile = Profile::FromBrowserContext(browser_context());
  const std::string& err_msg =
      SetWhitelistedPref(profile, arc::prefs::kVoiceInteractionEnabled,
                         base::Value(params->enabled));
  if (!err_msg.empty())
    return RespondNow(Error(err_msg));

  // |NOT_READY| means service not running
  // |STOPPED| means service running but UI not shown
  auto new_state = params->enabled
                       ? ash::mojom::VoiceInteractionState::STOPPED
                       : ash::mojom::VoiceInteractionState::NOT_READY;

  if (assistant_state_.voice_interaction_state() == new_state)
    return RespondNow(NoArguments());

  // Assistant service has not responded yet, set up a delayed timer to wait for
  // it and holder a reference to |this|. Also make sure we stop and respond
  // when timeout.
  expected_state_ = new_state;
  timeout_timer_.Start(
      FROM_HERE, base::TimeDelta::FromMilliseconds(params->timeout_ms),
      base::BindOnce(&AutotestPrivateSetAssistantEnabledFunction::Timeout,
                     this));
  return RespondLater();
}

void AutotestPrivateSetAssistantEnabledFunction::
    OnVoiceInteractionStatusChanged(ash::mojom::VoiceInteractionState state) {
  DCHECK(expected_state_);

  // The service could go through |NOT_READY| then to |STOPPED| during enable
  // flow if this API is called before the initial state is reported.
  if (expected_state_ != state)
    return;

  Respond(NoArguments());
  expected_state_.reset();
  timeout_timer_.AbandonAndStop();
}

void AutotestPrivateSetAssistantEnabledFunction::Timeout() {
  Respond(Error("Assistant service timed out"));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSendAssistantTextQueryFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSendAssistantTextQueryFunction::
    AutotestPrivateSendAssistantTextQueryFunction()
    : assistant_interaction_subscriber_binding_(this),
      result_(std::make_unique<base::DictionaryValue>()) {}

AutotestPrivateSendAssistantTextQueryFunction::
    ~AutotestPrivateSendAssistantTextQueryFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSendAssistantTextQueryFunction::Run() {
  DVLOG(1) << "AutotestPrivateSendAssistantTextQueryFunction";

  std::unique_ptr<api::autotest_private::SendAssistantTextQuery::Params> params(
      api::autotest_private::SendAssistantTextQuery::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  Profile* profile = Profile::FromBrowserContext(browser_context());
  ash::mojom::AssistantAllowedState allowed_state =
      assistant::IsAssistantAllowedForProfile(profile);
  if (allowed_state != ash::mojom::AssistantAllowedState::ALLOWED) {
    return RespondNow(Error(base::StringPrintf(
        "Assistant not allowed - state: %d", allowed_state)));
  }

  // Bind to Assistant service interface.
  service_manager::Connector* connector =
      content::BrowserContext::GetConnectorFor(profile);
  connector->BindInterface(chromeos::assistant::mojom::kServiceName,
                           &assistant_);

  // Subscribe to Assistant interaction events.
  chromeos::assistant::mojom::AssistantInteractionSubscriberPtr ptr;
  assistant_interaction_subscriber_binding_.Bind(mojo::MakeRequest(&ptr));
  assistant_->AddAssistantInteractionSubscriber(std::move(ptr));

  // Start text interaction with Assistant server.
  assistant_->StartTextInteraction(params->query, true);

  // Set up a delayed timer to wait for the query response and hold a reference
  // to |this| to avoid being destructed. Also make sure we stop and respond
  // when timeout.
  timeout_timer_.Start(
      FROM_HERE, base::TimeDelta::FromMilliseconds(params->timeout_ms),
      base::BindOnce(&AutotestPrivateSendAssistantTextQueryFunction::Timeout,
                     this));

  return RespondLater();
}

void AutotestPrivateSendAssistantTextQueryFunction::OnTextResponse(
    const std::string& response) {
  result_->SetKey("text", base::Value(response));
}

void AutotestPrivateSendAssistantTextQueryFunction::OnHtmlResponse(
    const std::string& response,
    const std::string& fallback) {
  result_->SetKey("htmlResponse", base::Value(response));
  result_->SetKey("htmlFallback", base::Value(fallback));
}

void AutotestPrivateSendAssistantTextQueryFunction::OnInteractionFinished(
    AssistantInteractionResolution resolution) {
  // Only return a result to the caller and stop the timer when |result_|
  // is not empty to avoid an early return before the entire interaction is
  // completed. This happens when sending queries to modify device settings,
  // e.g. "turn on bluetooth", which results in two rounds of interaction.
  if (result_->empty())
    return;

  if (resolution != AssistantInteractionResolution::kNormal) {
    Respond(Error("Interaction ends abnormally."));
    timeout_timer_.AbandonAndStop();
    return;
  }

  Respond(OneArgument(std::move(result_)));
  timeout_timer_.AbandonAndStop();
}

void AutotestPrivateSendAssistantTextQueryFunction::Timeout() {
  Respond(Error("Assistant response timeout."));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetWhitelistedPrefFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetWhitelistedPrefFunction::
    ~AutotestPrivateSetWhitelistedPrefFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetWhitelistedPrefFunction::Run() {
  DVLOG(1) << "AutotestPrivateSetWhitelistedPrefFunction";

  std::unique_ptr<api::autotest_private::SetWhitelistedPref::Params> params(
      api::autotest_private::SetWhitelistedPref::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  const std::string& pref_name = params->pref_name;
  const base::Value& value = *(params->value);

  Profile* profile = Profile::FromBrowserContext(browser_context());
  const std::string& err_msg = SetWhitelistedPref(profile, pref_name, value);

  if (!err_msg.empty())
    return RespondNow(Error(err_msg));

  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetCrostiniAppScaledFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetCrostiniAppScaledFunction::
    ~AutotestPrivateSetCrostiniAppScaledFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetCrostiniAppScaledFunction::Run() {
  std::unique_ptr<api::autotest_private::SetCrostiniAppScaled::Params> params(
      api::autotest_private::SetCrostiniAppScaled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  DVLOG(1) << "AutotestPrivateSetCrostiniAppScaledFunction " << params->app_id
           << " " << params->scaled;

  ChromeLauncherController* const controller =
      ChromeLauncherController::instance();
  if (!controller)
    return RespondNow(Error("Controller not available"));

  crostini::CrostiniRegistryService* registry_service =
      crostini::CrostiniRegistryServiceFactory::GetForProfile(
          controller->profile());
  if (!registry_service)
    return RespondNow(Error("Crostini registry not available"));

  registry_service->SetAppScaled(params->app_id, params->scaled);
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::
    AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction() = default;
AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::
    ~AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::Run() {
  auto params = api::autotest_private::EnsureWindowServiceClientHasDrawnWindow::
      Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  service_manager::Connector* connector =
      content::ServiceManagerConnection::GetForProcess()->GetConnector();
  connector->BindInterface(
      service_manager::ServiceFilter::ByName(ws::mojom::kServiceName),
      mojo::MakeRequest(&window_server_test_ptr_));
  window_server_test_ptr_->EnsureClientHasDrawnWindow(
      params->client_name,
      base::BindOnce(
          &AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::
              OnEnsureClientHasDrawnWindowCallback,
          this));

  timeout_timer_.Start(
      FROM_HERE, base::TimeDelta::FromMilliseconds(params->timeout_ms),
      base::BindOnce(
          &AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::
              OnTimeout,
          this));

  return RespondLater();
}

void AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::
    OnEnsureClientHasDrawnWindowCallback(bool success) {
  if (did_respond()) {
    LOG(ERROR) << "EnsureClientHasDrawnWindow returned after timeout: "
               << success;
    return;
  }

  Respond(OneArgument(std::make_unique<base::Value>(success)));
  timeout_timer_.AbandonAndStop();
}

void AutotestPrivateEnsureWindowServiceClientHasDrawnWindowFunction::
    OnTimeout() {
  if (did_respond())
    return;

  Respond(Error("EnsureWindowServiceClientHasDrawnWindowFunction timeout."));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetPrimaryDisplayScaleFactorFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetPrimaryDisplayScaleFactorFunction::
    ~AutotestPrivateGetPrimaryDisplayScaleFactorFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateGetPrimaryDisplayScaleFactorFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetPrimaryDisplayScaleFactorFunction";

  display::Display primary_display =
      display::Screen::GetScreen()->GetPrimaryDisplay();
  float scale_factor = primary_display.device_scale_factor();
  return RespondNow(OneArgument(std::make_unique<base::Value>(scale_factor)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateIsTabletModeEnabledFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateIsTabletModeEnabledFunction::
    ~AutotestPrivateIsTabletModeEnabledFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateIsTabletModeEnabledFunction::Run() {
  DVLOG(1) << "AutotestPrivateIsTabletModeEnabledFunction";

  return RespondNow(OneArgument(std::make_unique<base::Value>(
      TabletModeClient::Get()->tablet_mode_enabled())));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetTabletModeEnabledFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetTabletModeEnabledFunction::
    ~AutotestPrivateSetTabletModeEnabledFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetTabletModeEnabledFunction::Run() {
  DVLOG(1) << "AutotestPrivateSetTabletModeEnabledFunction";

  std::unique_ptr<api::autotest_private::SetTabletModeEnabled::Params> params(
      api::autotest_private::SetTabletModeEnabled::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);
  TabletModeClient::Get()->SetTabletModeEnabledForTesting(
      params->enabled,
      base::BindOnce(
          &AutotestPrivateSetTabletModeEnabledFunction::OnSetTabletModeEnabled,
          this));
  return RespondLater();
}

void AutotestPrivateSetTabletModeEnabledFunction::OnSetTabletModeEnabled(
    bool enabled) {
  Respond(OneArgument(std::make_unique<base::Value>(enabled)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateGetShelfAutoHideBehaviorFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateGetShelfAutoHideBehaviorFunction::
    AutotestPrivateGetShelfAutoHideBehaviorFunction() = default;

AutotestPrivateGetShelfAutoHideBehaviorFunction::
    ~AutotestPrivateGetShelfAutoHideBehaviorFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateGetShelfAutoHideBehaviorFunction::Run() {
  DVLOG(1) << "AutotestPrivateGetShelfAutoHideBehaviorFunction";

  std::unique_ptr<api::autotest_private::GetShelfAutoHideBehavior::Params>
      params(api::autotest_private::GetShelfAutoHideBehavior::Params::Create(
          *args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  service_manager::Connector* connector =
      content::ServiceManagerConnection::GetForProcess()->GetConnector();
  connector->BindInterface(ash::mojom::kServiceName, &shelf_controller_);

  int64_t display_id;
  if (!base::StringToInt64(params->display_id, &display_id)) {
    return RespondNow(
        Error("Invalid display_id. Expected string with numbers only. got %s",
              params->display_id));
  }

  shelf_controller_->GetAutoHideBehaviorForTesting(
      display_id,
      base::BindOnce(&AutotestPrivateGetShelfAutoHideBehaviorFunction::
                         OnGetShelfAutoHideBehaviorCompleted,
                     this));
  return RespondLater();
}

void AutotestPrivateGetShelfAutoHideBehaviorFunction::
    OnGetShelfAutoHideBehaviorCompleted(ash::ShelfAutoHideBehavior behavior) {
  std::string str_behavior;
  switch (behavior) {
    case ash::ShelfAutoHideBehavior::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS:
      str_behavior = "always";
      break;
    case ash::ShelfAutoHideBehavior::SHELF_AUTO_HIDE_BEHAVIOR_NEVER:
      str_behavior = "never";
      break;
    case ash::ShelfAutoHideBehavior::SHELF_AUTO_HIDE_ALWAYS_HIDDEN:
      str_behavior = "hidden";
      break;
  }
  Respond(OneArgument(std::make_unique<base::Value>(str_behavior)));
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateSetShelfAutoHideBehaviorFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateSetShelfAutoHideBehaviorFunction::
    AutotestPrivateSetShelfAutoHideBehaviorFunction() = default;

AutotestPrivateSetShelfAutoHideBehaviorFunction::
    ~AutotestPrivateSetShelfAutoHideBehaviorFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateSetShelfAutoHideBehaviorFunction::Run() {
  DVLOG(1) << "AutotestPrivateSetShelfAutoHideBehaviorFunction";

  std::unique_ptr<api::autotest_private::SetShelfAutoHideBehavior::Params>
      params(api::autotest_private::SetShelfAutoHideBehavior::Params::Create(
          *args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  service_manager::Connector* connector =
      content::ServiceManagerConnection::GetForProcess()->GetConnector();
  connector->BindInterface(ash::mojom::kServiceName, &shelf_controller_);

  ash::ShelfAutoHideBehavior behavior;
  if (params->behavior == "always") {
    behavior = ash::ShelfAutoHideBehavior::SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS;
  } else if (params->behavior == "never") {
    behavior = ash::ShelfAutoHideBehavior::SHELF_AUTO_HIDE_BEHAVIOR_NEVER;
  } else if (params->behavior == "hidden") {
    behavior = ash::ShelfAutoHideBehavior::SHELF_AUTO_HIDE_ALWAYS_HIDDEN;
  } else {
    return RespondNow(Error(
        "Invalid argument: '%s'. Expected: 'always', 'never' or 'hidden'.",
        params->behavior));
  }
  int64_t display_id;
  if (!base::StringToInt64(params->display_id, &display_id)) {
    return RespondNow(
        Error("Invalid display_id. Expected string with numbers only. got %s",
              params->display_id));
  }

  shelf_controller_->SetAutoHideBehaviorForTesting(
      display_id, behavior,
      base::BindOnce(&AutotestPrivateSetShelfAutoHideBehaviorFunction::
                         OnSetShelfAutoHideBehaviorCompleted,
                     this));
  return RespondLater();
}

void AutotestPrivateSetShelfAutoHideBehaviorFunction::
    OnSetShelfAutoHideBehaviorCompleted() {
  Respond(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateShowVirtualKeyboardIfEnabledFunction
///////////////////////////////////////////////////////////////////////////////

AutotestPrivateShowVirtualKeyboardIfEnabledFunction::
    AutotestPrivateShowVirtualKeyboardIfEnabledFunction() = default;
AutotestPrivateShowVirtualKeyboardIfEnabledFunction::
    ~AutotestPrivateShowVirtualKeyboardIfEnabledFunction() = default;

ExtensionFunction::ResponseAction
AutotestPrivateShowVirtualKeyboardIfEnabledFunction::Run() {
  if (!ui::IMEBridge::Get() ||
      !ui::IMEBridge::Get()->GetInputContextHandler() ||
      !ui::IMEBridge::Get()->GetInputContextHandler()->GetInputMethod()) {
    return RespondNow(NoArguments());
  }

  ui::IMEBridge::Get()
      ->GetInputContextHandler()
      ->GetInputMethod()
      ->ShowVirtualKeyboardIfEnabled();
  return RespondNow(NoArguments());
}

///////////////////////////////////////////////////////////////////////////////
// AutotestPrivateAPI
///////////////////////////////////////////////////////////////////////////////

static base::LazyInstance<BrowserContextKeyedAPIFactory<AutotestPrivateAPI>>::
    DestructorAtExit g_autotest_private_api_factory = LAZY_INSTANCE_INITIALIZER;

// static
BrowserContextKeyedAPIFactory<AutotestPrivateAPI>*
AutotestPrivateAPI::GetFactoryInstance() {
  return g_autotest_private_api_factory.Pointer();
}

template <>
KeyedService*
BrowserContextKeyedAPIFactory<AutotestPrivateAPI>::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new AutotestPrivateAPI();
}

AutotestPrivateAPI::AutotestPrivateAPI() : test_mode_(false) {}

AutotestPrivateAPI::~AutotestPrivateAPI() = default;

}  // namespace extensions
