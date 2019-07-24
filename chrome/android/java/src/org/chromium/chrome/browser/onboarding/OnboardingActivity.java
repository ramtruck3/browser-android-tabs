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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_onboarding);


        Intent intent = getIntent();
        if(intent!=null){
            onboardingType = intent.getIntExtra("onboarding_type",OnboardingPrefManager.NEW_USER_ONBOARDING);
        }

        OnboardingViewPagerAdapter onboardingViewPagerAdapter = new OnboardingViewPagerAdapter(getFragmentManager(), this, onboardingType);
        viewPager = findViewById(R.id.view_pager);
        viewPager.setAdapter(onboardingViewPagerAdapter);
    }

    @Override
    public void onSkip() {
        // OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);

        Calendar calender = Calendar.getInstance();
        calender.setTime(new Date());
        calender.add(Calendar.DATE, OnboardingPrefManager.getInstance().getPrefOnboardingSkipCount()==0?60:120);

        OnboardingPrefManager.getInstance().setPrefNextOnboardingDate(calender.getTimeInMillis());
        OnboardingPrefManager.getInstance().setPrefOnboardingSkipCount();
        finish();
    }

    @Override
    public void onNext() {
        viewPager.setCurrentItem(viewPager.getCurrentItem() + 1);
    }

    @Override
    public void onStartBrowsing() {
        String keyword = OnboardingPrefManager.selectedSearchEngine.getKeyword();
        String name = OnboardingPrefManager.selectedSearchEngine.getShortName();
        TemplateUrlService.getInstance().setSearchEngine(name, keyword, false);

        OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);
        finish();
    }

    @Override
    public void onDidntSeeAd() {
        viewPager.setCurrentItem(viewPager.getCurrentItem() + 1);
    }

    @Override
    public void onBackPressed() {

        // if (Constants.isNotification && viewPager.getCurrentItem() == 3) {

        // } else if (viewPager.getCurrentItem() > 0)
        //     viewPager.setCurrentItem(viewPager.getCurrentItem() - 1);
    }
}