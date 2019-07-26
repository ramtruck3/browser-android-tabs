package org.chromium.chrome.browser.onboarding;

import android.os.Bundle;
import android.support.annotation.NonNull;
import android.support.v4.view.ViewPager;
import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.view.animation.Animation;
import android.view.animation.TranslateAnimation;
import android.widget.Toast;
import android.content.Intent;
import java.util.Calendar;
import java.util.Date;

import org.chromium.chrome.R;

import org.chromium.chrome.browser.onboarding.NonSwipeableViewPager;
import org.chromium.chrome.browser.onboarding.OnboardingViewPagerAdapter;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;
import org.chromium.chrome.browser.search_engines.TemplateUrlService;

public class OnboardingActivity extends AppCompatActivity implements OnViewPagerAction {

    private NonSwipeableViewPager viewPager;
    private int onboardingType;
    private boolean fromSettings;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_onboarding);

        Intent intent = getIntent();
        if(intent!=null){
            onboardingType = intent.getIntExtra(OnboardingPrefManager.ONBOARDING_TYPE,OnboardingPrefManager.NEW_USER_ONBOARDING);
            fromSettings = intent.getBooleanExtra(OnboardingPrefManager.FROM_SETTINGS, false);
        }

        OnboardingViewPagerAdapter onboardingViewPagerAdapter = new OnboardingViewPagerAdapter(getFragmentManager(), this, onboardingType, fromSettings);
        viewPager = findViewById(R.id.view_pager);
        viewPager.setAdapter(onboardingViewPagerAdapter);
    }

    @Override
    public void onSkip() {

        if(!fromSettings){
            Calendar calender = Calendar.getInstance();
            calender.setTime(new Date());
            calender.add(Calendar.DATE, OnboardingPrefManager.getInstance().getPrefOnboardingSkipCount()==0?60:120);

            OnboardingPrefManager.getInstance().setPrefNextOnboardingDate(calender.getTimeInMillis());
            OnboardingPrefManager.getInstance().setPrefOnboardingSkipCount();
        }
        finish();
    }

    @Override
    public void onNext() {
        viewPager.setCurrentItem(viewPager.getCurrentItem() + 1);
    }

    @Override
    public void onStartBrowsing() {
        if(!fromSettings){
            String keyword = OnboardingPrefManager.selectedSearchEngine.getKeyword();
            String name = OnboardingPrefManager.selectedSearchEngine.getShortName();
            TemplateUrlService.getInstance().setSearchEngine(name, keyword, false);

            OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);
        }
        finish();
    }

    @Override
    public void onDidntSeeAd() {
        viewPager.setCurrentItem(viewPager.getCurrentItem() + 1);
    }

    @Override
    public void onBackPressed() {
        // kept it blank to disable back button
    }
}