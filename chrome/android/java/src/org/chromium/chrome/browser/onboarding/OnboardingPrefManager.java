package org.chromium.chrome.browser.onboarding;

import android.content.SharedPreferences;
import android.text.TextUtils;
import android.content.Context;
import android.content.Intent;
import android.app.PendingIntent;
import java.util.Locale;
import android.app.AlarmManager;
import java.lang.System;

import org.chromium.base.ContextUtils;
import org.chromium.chrome.browser.BraveRewardsPanelPopup;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.util.PackageUtils;
import org.chromium.chrome.browser.notifications.BraveOnboardingNotification;


/**
 * Provides information regarding onboarding enabled states.
 *
 */
public class OnboardingPrefManager {

    private static final String PREF_ONBOARDING = "onboarding";

    private static OnboardingPrefManager sInstance;

    private final SharedPreferences mSharedPreferences;

    public static final int NEW_USER_ONBOARDING = 0;
    public static final int EXISTING_USER_REWARDS_OFF_ONBOARDING = 1;
    public static final int EXISTING_USER_REWARDS_ON_ONBOARDING = 2;

    private OnboardingPrefManager() {
        mSharedPreferences = ContextUtils.getAppSharedPreferences();
    }

    /**
     * Returns the singleton instance of OnboardingPrefManager, creating it if needed.
     */
    public static OnboardingPrefManager getInstance() {
        if (sInstance == null) {
            sInstance = new OnboardingPrefManager();
        }
        return sInstance;
    }

    /**
     * Returns the user preference for whether the onboarding is enabled.
     *
     */
    public boolean getPrefOnboardingEnabled() {
        return mSharedPreferences.getBoolean(PREF_ONBOARDING, true);
    }

    /**
     * Sets the user preference for whether the onboarding is enabled.
     */
    public void setPrefOnboardingEnabled(boolean enabled) {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putBoolean(PREF_ONBOARDING, enabled);
        sharedPreferencesEditor.apply();
    }

    private boolean shouldShowNewUserOnboarding(Context context) {
        boolean shouldShow =
          getPrefOnboardingEnabled()
          && PackageUtils.isFirstInstall(context);

        return shouldShow;
    }

    private boolean shouldShowExistingUserOnboardingIfRewardsIsSwitchedOff(Context context) {
        boolean shouldShow =
          getPrefOnboardingEnabled()
          && isAdsAvailable()
          && !PackageUtils.isFirstInstall(context)
          && !BraveRewardsPanelPopup.isBraveRewardsEnabled()
          && ChromeFeatureList.isEnabled(ChromeFeatureList.BRAVE_REWARDS);

        return shouldShow;
    }

    private boolean shouldShowExistingUserOnboardingIfRewardsIsSwitchedOn(Context context) {
        boolean shouldShow =
          getPrefOnboardingEnabled()
          && isAdsAvailable()
          && !PackageUtils.isFirstInstall(context)
          && BraveRewardsPanelPopup.isBraveRewardsEnabled()
          && ChromeFeatureList.isEnabled(ChromeFeatureList.BRAVE_REWARDS);

        return shouldShow;
    }

    public boolean isAdsAvailable() {
        Locale locale = Locale.getDefault();
        return locale.toString().equals("en_US")
                || locale.toString().equals("en_CA")
                || locale.toString().equals("en_NZ")
                || locale.toString().equals("en_IE")
                || locale.toString().equals("en_AU")
                || locale.toString().equals("fr_CA")
                || locale.toString().equals("fr_FR")
                || locale.toString().equals("en_GB")
                || locale.toString().equals("de_DE");
    }

    public void showOnboarding(Context context){

        int onboardingType = -1;

        if(shouldShowNewUserOnboarding(context)){
            onboardingType = NEW_USER_ONBOARDING;
        }else if(shouldShowExistingUserOnboardingIfRewardsIsSwitchedOff(context)){
            onboardingType = EXISTING_USER_REWARDS_OFF_ONBOARDING;
        }else if(shouldShowExistingUserOnboardingIfRewardsIsSwitchedOn(context)){
            onboardingType = EXISTING_USER_REWARDS_ON_ONBOARDING;
        }

        Intent intent = new Intent(context, OnboardingActivity.class);
        intent.putExtra("onboarding_type",onboardingType);
        context.startActivity(intent);
    }

    public void onboardingNotification(Context context) {
        AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
        Intent intent = new Intent(context, BraveOnboardingNotification.class);
        am.set(
            AlarmManager.RTC_WAKEUP,
            System.currentTimeMillis(),
            PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT)
        );
    }
}
