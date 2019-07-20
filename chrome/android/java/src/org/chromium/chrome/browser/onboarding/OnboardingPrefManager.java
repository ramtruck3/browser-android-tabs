package org.chromium.chrome.browser.onboarding;

import android.content.SharedPreferences;
import android.text.TextUtils;

import org.chromium.base.ContextUtils;
import org.chromium.base.ObserverList;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.UrlConstants;
import org.chromium.chrome.browser.ntp.NewTabPage;
import org.chromium.chrome.browser.util.FeatureUtilities;

/**
 * Provides information regarding onboarding enabled states.
 *
 */
public class OnboardingPrefManager {

    private static final String PREF_ONBOARDING = "onboarding";

    private static OnboardingPrefManager sInstance;

    private final SharedPreferences mSharedPreferences;

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
}
