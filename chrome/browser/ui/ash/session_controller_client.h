// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_SESSION_CONTROLLER_CLIENT_H_
#define CHROME_BROWSER_UI_ASH_SESSION_CONTROLLER_CLIENT_H_

#include <vector>

#include "ash/public/interfaces/session_controller.mojom.h"
#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/policy/off_hours/device_off_hours_controller.h"
#include "chrome/browser/supervised_user/supervised_user_service_observer.h"
#include "chromeos/login/login_state/login_state.h"
#include "components/session_manager/core/session_manager_observer.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "mojo/public/cpp/bindings/binding.h"

class Profile;
class PrefChangeRegistrar;

namespace ash {
enum class AddUserSessionPolicy;
}

namespace user_manager {
class User;
}

// Updates session state etc to ash via SessionController interface and handles
// session related calls from ash.
// TODO(xiyuan): Update when UserSessionStateObserver is gone.
class SessionControllerClient
    : public ash::mojom::SessionControllerClient,
      public user_manager::UserManager::UserSessionStateObserver,
      public user_manager::UserManager::Observer,
      public session_manager::SessionManagerObserver,
      public SupervisedUserServiceObserver,
      public chromeos::LoginState::Observer,
      public content::NotificationObserver,
      public policy::off_hours::DeviceOffHoursController::Observer {
 public:
  SessionControllerClient();
  ~SessionControllerClient() override;

  void Init();

  static SessionControllerClient* Get();

  // Calls SessionController to prepare locking ash.
  void PrepareForLock(base::OnceClosure callback);

  // Calls SessionController to start locking ash. |callback| will be invoked
  // to indicate whether the lock is successful. If |locked| is true, the post
  // lock animation is finished and ash is fully locked. Otherwise, the lock
  // is failed somehow.
  using StartLockCallback = base::OnceCallback<void(bool locked)>;
  void StartLock(StartLockCallback callback);

  // Notifies SessionController that chrome lock animations are finished.
  void NotifyChromeLockAnimationsComplete();

  // Calls ash SessionController to run unlock animation.
  // |animation_finished_callback| will be invoked when the animation finishes.
  void RunUnlockAnimation(base::OnceClosure animation_finished_callback);

  // Asks the session controller to show the window teleportation dialog.
  void ShowTeleportWarningDialog(
      base::OnceCallback<void(bool, bool)> on_accept);

  // ash::mojom::SessionControllerClient:
  void RequestLockScreen() override;
  void RequestSignOut() override;
  void SwitchActiveUser(const AccountId& account_id) override;
  void CycleActiveUser(ash::CycleUserDirection direction) override;
  void ShowMultiProfileLogin() override;
  void EmitAshInitialized() override;

  // Returns true if a multi-profile user can be added to the session or if
  // multiple users are already signed in.
  static bool IsMultiProfileAvailable();

  // user_manager::UserManager::UserSessionStateObserver:
  void ActiveUserChanged(const user_manager::User* active_user) override;
  void UserAddedToSession(const user_manager::User* added_user) override;

  // user_manager::UserManager::Observer
  void OnUserImageChanged(const user_manager::User& user) override;

  // session_manager::SessionManagerObserver:
  void OnSessionStateChanged() override;

  // SupervisedUserServiceObserver:
  void OnCustodianInfoChanged() override;

  // chromeos::LoginState::Observer:
  void LoggedInStateChanged() override;

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // DeviceOffHoursController::Observer:
  void OnOffHoursEndTimeChanged() override;

  // TODO(xiyuan): Remove after SessionStateDelegateChromeOS is gone.
  static bool CanLockScreen();
  static bool ShouldLockScreenAutomatically();
  static ash::AddUserSessionPolicy GetAddUserSessionPolicy();
  static void DoLockScreen();
  static void DoSwitchActiveUser(const AccountId& account_id);
  static void DoCycleActiveUser(ash::CycleUserDirection direction);

  // Flushes the mojo pipe to ash.
  static void FlushForTesting();

 private:
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, CyclingThreeUsers);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, SendUserSession);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, SupervisedUser);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, UserPrefsChange);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, SessionLengthLimit);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, DeviceOwner);
  FRIEND_TEST_ALL_PREFIXES(SessionControllerClientTest, UserBecomesDeviceOwner);

  // Called when the login profile is ready.
  void OnLoginUserProfilePrepared(Profile* profile);

  // Sends the user session info for a given profile.
  void SendUserSessionForProfile(Profile* profile);

  // Connects to the |session_controller_| interface.
  void ConnectToSessionController();

  // Sends session info to ash.
  void SendSessionInfoIfChanged();

  // Sends the user session info.
  void SendUserSession(const user_manager::User& user);

  // Sends the order of user sessions to ash.
  void SendUserSessionOrder();

  // Sends the session length time limit to ash considering two policies which
  // restrict session length: "SessionLengthLimit" and "OffHours". Send limit
  // from "SessionLengthLimit" policy if "OffHours" mode is off now or if
  // "SessionLengthLimit" policy will be ended earlier than "OffHours" mode.
  // Send limit from "OffHours" policy if "SessionLengthLimit" policy is unset
  // or if "OffHours" mode will be ended earlier than "SessionLengthLimit"
  // policy.
  void SendSessionLengthLimit();

  // Binds to the client interface.
  mojo::Binding<ash::mojom::SessionControllerClient> binding_;

  // SessionController interface in ash. Holding the interface pointer keeps the
  // pipe alive to receive mojo return values.
  ash::mojom::SessionControllerPtr session_controller_;

  // Whether the primary user session info is sent to ash.
  bool primary_user_session_sent_ = false;

  // If the session is for a supervised user, the profile of that user.
  // Chrome OS only supports a single supervised user in a session.
  Profile* supervised_user_profile_ = nullptr;

  content::NotificationRegistrar registrar_;

  // Pref change observers to update session info when a relevant user pref
  // changes. There is one observer per user and they have no particular order,
  // i.e. they don't much the user session order.
  std::vector<std::unique_ptr<PrefChangeRegistrar>> pref_change_registrars_;

  // Observes changes to Local State prefs.
  std::unique_ptr<PrefChangeRegistrar> local_state_registrar_;

  // Used to suppress duplicate IPCs to ash.
  ash::mojom::SessionInfoPtr last_sent_session_info_;
  ash::mojom::UserSessionPtr last_sent_user_session_;

  base::WeakPtrFactory<SessionControllerClient> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SessionControllerClient);
};

#endif  // CHROME_BROWSER_UI_ASH_SESSION_CONTROLLER_CLIENT_H_
