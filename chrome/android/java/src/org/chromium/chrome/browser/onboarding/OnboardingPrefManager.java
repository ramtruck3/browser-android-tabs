package org.chromium.chrome.browser.onboarding;

import android.content.SharedPreferences;
import android.text.TextUtils;
import android.content.Context;
import android.content.Intent;
import android.app.PendingIntent;
import java.util.Locale;
import android.app.AlarmManager;
import java.lang.System;
import android.widget.Toast;

import java.util.HashMap;
import java.util.Map;

import org.chromium.base.ContextUtils;
import org.chromium.chrome.browser.BraveRewardsPanelPopup;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.util.PackageUtils;
import org.chromium.chrome.browser.BraveAdsNativeHelper;
import org.chromium.chrome.browser.notifications.BraveOnboardingNotification;
import org.chromium.chrome.browser.search_engines.TemplateUrl;
import org.chromium.chrome.browser.search_engines.TemplateUrlService;


/**
 * Provides information regarding onboarding enabled states.
 *
 */
public class OnboardingPrefManager {

    private static final String PREF_ONBOARDING = "onboarding";
    private static final String PREF_NEXT_ONBOARDING_DATE = "next_onboarding_date";
    private static final String PREF_ONBOARDING_SKIP_COUNT = "onboarding_skip_count";
    public static final String ONBOARDING_TYPE = "onboarding_type";

    private static OnboardingPrefManager sInstance;

    private final SharedPreferences mSharedPreferences;

    public static final int NEW_USER_ONBOARDING = 0;
    public static final int EXISTING_USER_REWARDS_OFF_ONBOARDING = 1;
    public static final int EXISTING_USER_REWARDS_ON_ONBOARDING = 2;

    public static TemplateUrl selectedSearchEngine = TemplateUrlService.getInstance().getDefaultSearchEngineTemplateUrl();

    private static boolean isOnboardingNotificationShown;

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

    public long getPrefNextOnboardingDate() {
        return mSharedPreferences.getLong(PREF_NEXT_ONBOARDING_DATE, 0);
    }

    public void setPrefNextOnboardingDate(long nextDate) {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putLong(PREF_NEXT_ONBOARDING_DATE, nextDate);
        sharedPreferencesEditor.apply();
    }

    public int getPrefOnboardingSkipCount() {
        return mSharedPreferences.getInt(PREF_ONBOARDING_SKIP_COUNT, 0);
    }

    public void setPrefOnboardingSkipCount() {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putInt(PREF_ONBOARDING_SKIP_COUNT, getPrefOnboardingSkipCount()+1);
        sharedPreferencesEditor.apply();
    }

    public boolean isOnboardingNotificationShown() {
        // if(isOnboardingNotificationShown==null){
        //   isOnboardingNotificationShown = false;
        // }
        return isOnboardingNotificationShown;
    }

    public void setOnboardingNotificationShown(boolean isShown) {
        isOnboardingNotificationShown = isShown;
    }

    public boolean showOnboardingForSkip(){
      boolean shouldShow = 
            getPrefNextOnboardingDate()==0 
            || (getPrefNextOnboardingDate() > 0 && System.currentTimeMillis() > getPrefNextOnboardingDate());
      return shouldShow;
    } 

    private boolean shouldShowNewUserOnboarding(Context context) {
        boolean shouldShow =
          getPrefOnboardingEnabled()
          && showOnboardingForSkip()
          && PackageUtils.isFirstInstall(context);

        return shouldShow;
    }

    private boolean shouldShowExistingUserOnboardingIfRewardsIsSwitchedOff(Context context) {
        boolean shouldShow =
          getPrefOnboardingEnabled()
          && showOnboardingForSkip()
          && isAdsAvailableNewLocale()
          && !PackageUtils.isFirstInstall(context)
          && !BraveRewardsPanelPopup.isBraveRewardsEnabled()
          && !BraveAdsNativeHelper.nativeIsBraveAdsEnabled(Profile.getLastUsedProfile())
          && ChromeFeatureList.isEnabled(ChromeFeatureList.BRAVE_REWARDS);

        return shouldShow;
    }

    private boolean shouldShowExistingUserOnboardingIfRewardsIsSwitchedOn(Context context) {
        boolean shouldShow =
          getPrefOnboardingEnabled()
          && showOnboardingForSkip()
          && isAdsAvailableNewLocale()
          && !PackageUtils.isFirstInstall(context)
          && BraveRewardsPanelPopup.isBraveRewardsEnabled()
          && !BraveAdsNativeHelper.nativeIsBraveAdsEnabled(Profile.getLastUsedProfile())
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

    public boolean isAdsAvailableNewLocale(){
      Locale locale = Locale.getDefault();
        return locale.toString().equals("en_NZ")
                || locale.toString().equals("en_IE")
                || locale.toString().equals("en_AU");
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

        if(onboardingType>=0){
          Intent intent = new Intent(context, OnboardingActivity.class);
          intent.putExtra(ONBOARDING_TYPE,onboardingType);
          context.startActivity(intent);
        }
    }

    public void onboardingNotification(Context context) {
      if(!isOnboardingNotificationShown()){
          AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
          Intent intent = new Intent(context, BraveOnboardingNotification.class);
          am.set(
              AlarmManager.RTC_WAKEUP,
              System.currentTimeMillis(),
              PendingIntent.getBroadcast(context, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT)
          );
          setOnboardingNotificationShown(true);
      }
    }

    public static Map<String,SearchEngineEnum> searchEngineMap = new HashMap<String, SearchEngineEnum>() {{
        put("Google", SearchEngineEnum.GOOGLE);
        put("DuckDuckGo", SearchEngineEnum.DUCKDUCKGO);
        put("DuckDuckGo Light", SearchEngineEnum.DUCKDUCKGOLIGHT);
        put("Qwant", SearchEngineEnum.QWANT);
        put("Bing", SearchEngineEnum.BING);
        put("StartPage", SearchEngineEnum.STARTPAGE);
        put("Yandex", SearchEngineEnum.YANDEX);
    }};

}
