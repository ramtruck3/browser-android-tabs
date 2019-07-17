package org.chromium.chrome.browser.onboarding;

import android.app.Fragment;
import android.app.FragmentManager;
import android.support.v13.app.FragmentPagerAdapter;

import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.browser.onboarding.SearchEngineOnboardingFragment;

public class OnboardingViewPagerAdapter extends FragmentPagerAdapter {

    private final OnViewPagerAction onViewPagerAction;

    public OnboardingViewPagerAdapter(FragmentManager fm, OnViewPagerAction onViewPagerAction) {
        super(fm);
        this.onViewPagerAction = onViewPagerAction;
    }

    @Override
    public Fragment getItem(int position) {
        switch (position) {
            case 0:
                SearchEngineOnboardingFragment searchEngineOnboardingFragment = new SearchEngineOnboardingFragment();
                searchEngineOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return searchEngineOnboardingFragment;
            case 1:
                SearchEngineOnboardingFragment searchEngineOnboardingFragment1 = new SearchEngineOnboardingFragment();
                searchEngineOnboardingFragment1.setOnViewPagerAction(onViewPagerAction);
                return searchEngineOnboardingFragment1;
            case 2:
                SearchEngineOnboardingFragment searchEngineOnboardingFragment2 = new SearchEngineOnboardingFragment();
                searchEngineOnboardingFragment2.setOnViewPagerAction(onViewPagerAction);
                return searchEngineOnboardingFragment2;
            default:
                return null;
        }

    }

    @Override
    public int getCount() {
        return 3;
    }
}