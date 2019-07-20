package org.chromium.chrome.browser.onboarding;

import android.os.Bundle;
import android.support.annotation.NonNull;
import android.support.v4.view.ViewPager;
import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.view.animation.Animation;
import android.view.animation.TranslateAnimation;
import android.widget.Toast;

import org.chromium.chrome.R;

import org.chromium.chrome.browser.onboarding.NonSwipeableViewPager;
import org.chromium.chrome.browser.onboarding.OnboardingViewPagerAdapter;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;

public class OnboardingActivity extends AppCompatActivity implements OnViewPagerAction {

    private NonSwipeableViewPager viewPager;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_onboarding);
        OnboardingViewPagerAdapter onboardingViewPagerAdapter = new OnboardingViewPagerAdapter(getFragmentManager(), this);
        viewPager = findViewById(R.id.view_pager);
        viewPager.setAdapter(onboardingViewPagerAdapter);
    }

    @Override
    public void onSkip() {
        // Toast.makeText(this, "On Skip", Toast.LENGTH_SHORT).show();
        OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);
        finish();
    }

    @Override
    public void onNext() {
        viewPager.setCurrentItem(viewPager.getCurrentItem() + 1);
    }

    @Override
    public void onStartBrowsing() {
        // Toast.makeText(this, "On Start browser", Toast.LENGTH_SHORT).show();
        OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);
        finish();
    }

    @Override
    public void onDidntSeeAd() {
        viewPager.setCurrentItem(viewPager.getCurrentItem() + 1);
    }

    @Override
    public void onBackPressed() {

        if (Constants.isNotification && viewPager.getCurrentItem() == 3) {

        } else if (viewPager.getCurrentItem() > 0)
            viewPager.setCurrentItem(viewPager.getCurrentItem() - 1);
    }
}