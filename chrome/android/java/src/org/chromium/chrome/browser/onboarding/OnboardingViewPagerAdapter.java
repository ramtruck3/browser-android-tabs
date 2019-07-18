package org.chromium.chrome.browser.onboarding;

import android.app.Fragment;
import android.app.FragmentManager;
import android.support.v13.app.FragmentPagerAdapter;

import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.browser.onboarding.SearchEngineOnboardingFragment;
import org.chromium.chrome.browser.onboarding.BraveShieldsOnboardingFragment;
import org.chromium.chrome.browser.onboarding.BraveRewardsOnboardingFragment;

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
                BraveShieldsOnboardingFragment braveShieldsOnboardingFragment = new BraveShieldsOnboardingFragment();
                braveShieldsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return braveShieldsOnboardingFragment;
            case 2:
                BraveRewardsOnboardingFragment braveRewardsOnboardingFragment = new BraveRewardsOnboardingFragment();
                braveRewardsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return braveRewardsOnboardingFragment;
            case 3:
                BraveAdsOnboardingFragment braveAdsOnboardingFragment = new BraveAdsOnboardingFragment();
                braveAdsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return braveAdsOnboardingFragment;
            case 4:
                TroubleshootingOnboardingFragment troubleshootingOnboardingFragment = new TroubleshootingOnboardingFragment();
                troubleshootingOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return troubleshootingOnboardingFragment;
            default:
                return null;
        }

    }

    @Override
    public int getCount() {
        return 5;
    }
}