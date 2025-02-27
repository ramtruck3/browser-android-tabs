// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_SCREEN_TIME_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_SCREEN_TIME_CONTROLLER_H_

#include <memory>
#include <string>

#include "ash/public/interfaces/login_screen.mojom.h"
#include "base/memory/scoped_refptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/child_accounts/parent_access_code/parent_access_service.h"
#include "chrome/browser/chromeos/child_accounts/time_limit_notifier.h"
#include "chrome/browser/chromeos/child_accounts/usage_time_limit_processor.h"
#include "chrome/browser/chromeos/child_accounts/usage_time_state_notifier.h"
#include "chromeos/dbus/system_clock/system_clock_client.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/session_manager/core/session_manager_observer.h"

class PrefRegistrySimple;
class PrefService;

namespace base {
class Clock;
class TickClock;
class OneShotTimer;
class SequencedTaskRunner;
}  // namespace base

namespace content {
class BrowserContext;
}

namespace chromeos {

// The controller to track each user's screen time usage and inquiry time limit
// processor (a component to calculate state based on policy settings and time
// usage) when necessary to determine the current lock screen state.
// Schedule notifications and lock/unlock screen based on the processor output.
class ScreenTimeController
    : public KeyedService,
      public parent_access::ParentAccessService::Delegate,
      public session_manager::SessionManagerObserver,
      public UsageTimeStateNotifier::Observer,
      public system::TimezoneSettings::Observer,
      public SystemClockClient::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Called when daily screen time limit is |kUsageTimeLimitWarningTime| or
    // less to finish.
    virtual void UsageTimeLimitWarning() = 0;
  };

  // Registers preferences.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  explicit ScreenTimeController(content::BrowserContext* context);
  ~ScreenTimeController() override;

  // Returns the child's screen time duration. This is how long the child has
  // used the device today (since the last reset).
  virtual base::TimeDelta GetScreenTimeDuration();

  // parent_access::ParentAccessService::Delegate:
  void OnAccessCodeValidation(bool result) override;

  // Method intended for testing purposes only.
  void SetClocksForTesting(
      const base::Clock* clock,
      const base::TickClock* tick_clock,
      scoped_refptr<base::SequencedTaskRunner> task_runner);

  // Call UsageTimeLimitWarning for each observer for testing.
  void NotifyUsageTimeLimitWarningForTesting();

  // Adds or removes an observer.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // Call time limit processor for new state.
  void CheckTimeLimit(const std::string& source);

  // Request to lock the screen and show the time limits message when the screen
  // is locked.
  void ForceScreenLockByPolicy();

  // Enables the time limits message in the lock screen and performs tasks that
  // need to run after the screen is locked.
  // |active_policy|: Which policy is locking the device, only valid when
  //                  |visible| is true.
  // |next_unlock_time|: When user will be able to unlock the screen, only valid
  //                     when |visible| is true.
  void OnScreenLockByPolicy(usage_time_limit::PolicyType active_policy,
                            base::Time next_unlock_time);

  // Disables the time limits message in the lock screen.
  void OnScreenLockByPolicyEnd();

  // Converts the active policy to its equivalent on the ash enum.
  base::Optional<ash::mojom::AuthDisabledReason> ConvertLockReason(
      usage_time_limit::PolicyType active_policy);

  // Called when the policy of time limits changes.
  void OnPolicyChanged();

  // Reset any currently running timers.
  void ResetStateTimers();
  void ResetInSessionTimers();
  void ResetWarningTimers();

  // Schedule a call for UsageTimeLimitWarning.
  void ScheduleUsageTimeLimitWarning(const usage_time_limit::State& state);

  // Save the |state| to |prefs::kScreenTimeLastState|.
  void SaveCurrentStateToPref(const usage_time_limit::State& state);

  // Get the last calculated |state| from |prefs::kScreenTimeLastState|, if it
  // exists.
  base::Optional<usage_time_limit::State> GetLastStateFromPref();

  // Called when the usage time limit is |kUsageTimeLimitWarningTime| or less to
  // finish. It should call the method UsageTimeLimitWarning for each observer.
  void UsageTimeLimitWarning();

  // Converts a usage_time_limit::PolicyType to its TimeLimitNotifier::LimitType
  // equivalent.
  base::Optional<TimeLimitNotifier::LimitType> ConvertPolicyType(
      usage_time_limit::PolicyType policy_type);

  // Initializes parent access service if it does not already exist. It requires
  // LoginScreenClient to be created.
  void InitializeParentAccessServiceIfNeeded();

  // session_manager::SessionManagerObserver:
  void OnSessionStateChanged() override;

  // UsageTimeStateNotifier::Observer:
  void OnUsageTimeStateChange(
      const UsageTimeStateNotifier::UsageTimeState state) override;

  // system::TimezoneSettings::Observer:
  void TimezoneChanged(const icu::TimeZone& timezone) override;

  // chromeos::SystemClockClient::Observer:
  void SystemClockUpdated() override;

  content::BrowserContext* context_;
  PrefService* pref_service_;

  base::ObserverList<Observer> observers_;

  // Points to the base::DefaultClock by default.
  const base::Clock* clock_;

  // Validates parent access codes. Informs registered delegate about validation
  // results.
  std::unique_ptr<parent_access::ParentAccessService> parent_access_service_;

  // Timer scheduled for when the next lock screen state change event is
  // expected to happen, e.g. when bedtime is over or the usage limit ends.
  std::unique_ptr<base::OneShotTimer> next_state_timer_;

  // Timer to schedule the usage time limit warning and call the
  // UsageTimeLimitWarning for each observer. This should happen
  // |kUsageTimeLimitWarningTime| minutes or less before the device is locked by
  // usage limit.
  std::unique_ptr<base::OneShotTimer> usage_time_limit_warning_timer_;

  // Contains the last time limit policy processed by this class. Used to
  // generate notifications when the policy changes.
  std::unique_ptr<base::DictionaryValue> last_policy_;

  // Used to set up timers when a time limit is approaching.
  TimeLimitNotifier time_limit_notifier_;

  PrefChangeRegistrar pref_change_registrar_;

  DISALLOW_COPY_AND_ASSIGN(ScreenTimeController);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_SCREEN_TIME_CONTROLLER_H_
