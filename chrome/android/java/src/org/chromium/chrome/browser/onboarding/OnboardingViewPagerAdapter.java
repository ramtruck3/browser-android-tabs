package org.chromium.chrome.browser.onboarding;

import android.app.Fragment;
import android.app.FragmentManager;
import android.support.v13.app.FragmentPagerAdapter;

import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;

public class OnboardingViewPagerAdapter extends FragmentPagerAdapter {

    private final OnViewPagerAction onViewPagerAction;
    private final int onboardingType;

    public OnboardingViewPagerAdapter(FragmentManager fm, OnViewPagerAction onViewPagerAction, int onboardingType) {
        super(fm);
        this.onViewPagerAction = onViewPagerAction;
        this.onboardingType = onboardingType;
    }

    @Override
    public Fragment getItem(int position) {
        switch(onboardingType){
            case OnboardingPrefManager.NEW_USER_ONBOARDING:
                return newUserOnboarding(position);
            case OnboardingPrefManager.EXISTING_USER_REWARDS_OFF_ONBOARDING:
                return existingUserRewardsOffOnboarding(position);
            case OnboardingPrefManager.EXISTING_USER_REWARDS_ON_ONBOARDING:
                return existingUserRewardsOnOnboarding(position);
            default:
                return null;
        }
    }

    @Override
    public int getCount() {
        if(onboardingType==OnboardingPrefManager.NEW_USER_ONBOARDING){
            return 5;
        }else{
            return 3;
        }
    }

    private Fragment newUserOnboarding(int position){
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

    private Fragment existingUserRewardsOffOnboarding(int position){
        switch (position) {
            case 0:
                BraveRewardsOnboardingFragment braveRewardsOnboardingFragment = new BraveRewardsOnboardingFragment();
                braveRewardsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                braveRewardsOnboardingFragment.setOnboardingType(onboardingType);
                return braveRewardsOnboardingFragment;
            case 1:
                BraveAdsOnboardingFragment braveAdsOnboardingFragment = new BraveAdsOnboardingFragment();
                braveAdsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return braveAdsOnboardingFragment;
            case 2:
                TroubleshootingOnboardingFragment troubleshootingOnboardingFragment = new TroubleshootingOnboardingFragment();
                troubleshootingOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return troubleshootingOnboardingFragment;
            default:
                return null;
        }
    }

    private Fragment existingUserRewardsOnOnboarding(int position){
        switch (position) {
            case 0:
                BraveRewardsOnboardingFragment braveRewardsOnboardingFragment = new BraveRewardsOnboardingFragment();
                braveRewardsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                braveRewardsOnboardingFragment.setOnboardingType(onboardingType);
                return braveRewardsOnboardingFragment;
            case 1:
                BraveAdsOnboardingFragment braveAdsOnboardingFragment = new BraveAdsOnboardingFragment();
                braveAdsOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return braveAdsOnboardingFragment;
            case 2:
                TroubleshootingOnboardingFragment troubleshootingOnboardingFragment = new TroubleshootingOnboardingFragment();
                troubleshootingOnboardingFragment.setOnViewPagerAction(onViewPagerAction);
                return troubleshootingOnboardingFragment;
            default:
                return null;
        }
    }

}