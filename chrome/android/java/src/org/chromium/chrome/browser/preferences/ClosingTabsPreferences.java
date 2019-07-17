package org.chromium.chrome.browser.preferences;

import android.os.Bundle;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceFragment;

import org.chromium.chrome.browser.partnercustomizations.ClosingTabsManager;

import org.chromium.base.ContextUtils;

import org.chromium.chrome.R;

public class ClosingTabsPreferences extends PreferenceFragment {
    private static final String PREF_CLOSING_TABS_SWITCH = "closing_tabs_switch";

    private ClosingTabsManager mClosingTabsManager;
    private ChromeSwitchPreference mClosingTabsSwitch;

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        mClosingTabsManager = ClosingTabsManager.getInstance();

        getActivity().setTitle(R.string.prefs_closing_all_tabs_closes_brave_title);
        PreferenceUtils.addPreferencesFromResource(this, R.xml.closing_tabs_preferences);

        mClosingTabsSwitch = (ChromeSwitchPreference) findPreference(PREF_CLOSING_TABS_SWITCH);

        boolean isClosingTabsEnabled = mClosingTabsManager.getPrefClosingAllTabsClosesBraveEnabled();
        mClosingTabsSwitch.setChecked(isClosingTabsEnabled);

        mClosingTabsSwitch.setOnPreferenceChangeListener(new OnPreferenceChangeListener() {
            @Override
            public boolean onPreferenceChange(Preference preference, Object newValue) {
                mClosingTabsManager.setPrefClosingAllTabsClosesBraveEnabled((boolean) newValue);
                return true;
            }
        });
    }
}