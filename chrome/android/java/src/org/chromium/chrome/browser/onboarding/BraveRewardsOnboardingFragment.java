package org.chromium.chrome.browser.onboarding;


import android.app.Fragment;
import android.os.Bundle;
import android.text.Html;
import android.text.Spanned;
import android.text.method.LinkMovementMethod;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.AnimationUtils;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import android.text.style.ClickableSpan;
import android.text.style.ForegroundColorSpan;
import android.text.SpannableString;
import android.text.TextPaint;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;

import android.support.annotation.NonNull;

import org.chromium.chrome.browser.onboarding.Constants;
import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;
import org.chromium.chrome.browser.BraveAdsNativeHelper;
import org.chromium.chrome.browser.BraveRewardsNativeWorker;
import org.chromium.chrome.browser.BraveRewardsPanelPopup;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.util.PackageUtils;
import org.chromium.chrome.browser.search_engines.TemplateUrlService;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;
import org.chromium.chrome.browser.customtabs.CustomTabActivity;
import org.chromium.chrome.R;

import static org.chromium.chrome.browser.util.AnimationUtil.fadeInView;
import static org.chromium.chrome.browser.util.AnimationUtil.fadeOutView;

public class BraveRewardsOnboardingFragment extends Fragment implements View.OnTouchListener {

    private OnViewPagerAction onViewPagerAction;

    private ImageView bgImage;

    private TextView tvTitle, tvText, tvAgree;

    private CheckBox chkAgreeTerms;

    private LinearLayout termAndAgreeLayout;

    private Button btnSkip, btnNext;
    private boolean isAgree;

    private static final String BRAVE_TERMS_PAGE = "https://basicattentiontoken.org/user-terms-of-service/";

    private int onboardingType = OnboardingPrefManager.NEW_USER_ONBOARDING;

    public BraveRewardsOnboardingFragment() {
        // Required empty public constructor
    }


    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {

        isAgree=false;

        // Inflate the layout for this fragment
        View root = inflater.inflate(R.layout.fragment_brave_rewards_onboarding, container, false);

        root.setOnTouchListener(this);

        initializeViews(root);

        setActions();

        return root;
    }

    @Override
    public boolean onTouch(View view, MotionEvent motionEvent) {
        if (isAgree && chkAgreeTerms != null && !chkAgreeTerms.isChecked()) {
            chkAgreeTerms.startAnimation(AnimationUtils.loadAnimation(getActivity(), R.anim.shake));
        }
        return true;
    }

    private void initializeViews(View root) {

        bgImage = root.findViewById(R.id.bg_image);

        tvTitle = root.findViewById(R.id.section_title);
        tvText = root.findViewById(R.id.section_text);

        termAndAgreeLayout = root.findViewById(R.id.terms_agree_layout);

        tvAgree = root.findViewById(R.id.agree_text);

        chkAgreeTerms = root.findViewById(R.id.chk_agree_terms);

        btnSkip = root.findViewById(R.id.btn_skip);
        btnNext = root.findViewById(R.id.btn_next);
    }

    private void setActions() {

        btnSkip.setText(getResources().getString(R.string.no_thanks));

        Spanned textToInsert;

        if(onboardingType==OnboardingPrefManager.EXISTING_USER_REWARDS_ON_ONBOARDING){

            bgImage.setImageResource(R.drawable.android_br_on);

            tvTitle.setText(getResources().getString(R.string.brave_ads_existing_user_offer_title));

            String braveRewardsText = "<b>" + getResources().getString(R.string.earn_tokens) + "</b> " + getResources().getString(R.string.brave_rewards_onboarding_text2);
            textToInsert = Constants.spannedFromHtmlString(braveRewardsText);
            tvText.setText(textToInsert);

            btnNext.setText(getResources().getString(R.string.turn_on));
        }else{
            String braveRewardsText = "<b>" + getResources().getString(R.string.earn_tokens) + "</b> " + getResources().getString(R.string.brave_rewards_onboarding_text);
            textToInsert = Constants.spannedFromHtmlString(braveRewardsText);
            tvText.setText(textToInsert);
        }

        String termsText = getResources().getString(R.string.terms_text) + "<br/>" + getResources().getString(R.string.terms_of_service)+ ".";
        Spanned textToAgree = Constants.spannedFromHtmlString(termsText);
        SpannableString ss = new SpannableString(textToAgree.toString());

        ClickableSpan clickableSpan = new ClickableSpan() {
            @Override
            public void onClick(@NonNull View textView) {
                CustomTabActivity.showInfoPage(getActivity(), BRAVE_TERMS_PAGE);
            }
            @Override
            public void updateDrawState(@NonNull TextPaint ds) {
                super.updateDrawState(ds);
                ds.setUnderlineText(false);
            }
        };

        ss.setSpan(clickableSpan, 24, ss.length()-1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);

        ForegroundColorSpan foregroundSpan = new ForegroundColorSpan(getResources().getColor(R.color.orange));
        ss.setSpan(foregroundSpan, 24, ss.length()-1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        tvAgree.setMovementMethod(LinkMovementMethod.getInstance());
        tvAgree.setText(ss);

        chkAgreeTerms.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton compoundButton, boolean isChecked) {
                if (isChecked) {
                    btnNext.setCompoundDrawablesWithIntrinsicBounds(0, 0, R.drawable.chevron_right, 0);
                    btnNext.setTextColor(getResources().getColor(R.color.orange));
                } else {
                    btnNext.setCompoundDrawablesWithIntrinsicBounds(0, 0, R.drawable.chevron_right_inactive, 0);
                    btnNext.setTextColor(getResources().getColor(R.color.disable_text_color));
                }
            }
        });

        btnSkip.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {

                if(onboardingType==OnboardingPrefManager.EXISTING_USER_REWARDS_ON_ONBOARDING){
                    onViewPagerAction.onSkip();
                }else{
                    if (isAgree) {
                    fadeOutView(termAndAgreeLayout);
                    fadeOutView(tvTitle);
                    fadeOutView(tvText);

                    chkAgreeTerms.setChecked(false);

                    btnSkip.setText(getResources().getString(R.string.no_thanks));
                    btnNext.setText(getResources().getString(R.string.join));
                    btnNext.setCompoundDrawablesWithIntrinsicBounds(0, 0, R.drawable.chevron_right, 0);
                    btnNext.setTextColor(getResources().getColor(R.color.orange));

                    tvTitle.setText(getResources().getString(R.string.brave_rewards_onboarding_title));
                    tvText.setText(textToInsert);
                    fadeInView(tvTitle);
                    fadeInView(tvText);

                    bgImage.setVisibility(View.VISIBLE);

                    isAgree = false;

                    } else {
                        onViewPagerAction.onSkip();
                    }
                }
            }
        });

        btnNext.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(onboardingType==OnboardingPrefManager.EXISTING_USER_REWARDS_ON_ONBOARDING){
                    BraveAdsNativeHelper.nativeSetAdsEnabled(Profile.getLastUsedProfile());
                    onViewPagerAction.onNext();
                }else{
                    if (!isAgree) {
                    fadeOutView(tvTitle);
                    tvText.setVisibility(View.GONE);

                    btnSkip.setText(getResources().getString(R.string.do_not_agree));
                    btnNext.setText(getResources().getString(R.string.agree));

                    tvTitle.setText(getResources().getString(R.string.terms_title));
                    fadeInView(tvTitle);
                    fadeInView(termAndAgreeLayout);

                    bgImage.setVisibility(View.VISIBLE);
                    termAndAgreeLayout.setVisibility(View.VISIBLE);

                    if (!chkAgreeTerms.isChecked()) {
                        btnNext.setCompoundDrawablesWithIntrinsicBounds(0, 0, R.drawable.chevron_right_inactive, 0);
                        btnNext.setTextColor(getResources().getColor(R.color.disable_text_color));
                    }

                    isAgree = true;

                    } else {
                        if (!chkAgreeTerms.isChecked()) {
                            chkAgreeTerms.startAnimation(AnimationUtils.loadAnimation(getActivity(), R.anim.shake));
                        } else {
                            BraveRewardsNativeWorker braveRewardsNativeWorker = BraveRewardsNativeWorker.getInstance();
                            braveRewardsNativeWorker.GetRewardsMainEnabled();
                            braveRewardsNativeWorker.CreateWallet();

                            if (PackageUtils.isFirstInstall(getActivity()) && !OnboardingPrefManager.getInstance().isAdsAvailable()) {
                                String keyword = OnboardingPrefManager.selectedSearchEngine.getKeyword();
                                String name = OnboardingPrefManager.selectedSearchEngine.getShortName();
                                TemplateUrlService.getInstance().setSearchEngine(name, keyword, false);

                                OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);
                                getActivity().finish();
                            } else {
                                // Enable ads
                                BraveAdsNativeHelper.nativeSetAdsEnabled(Profile.getLastUsedProfile());
                                onViewPagerAction.onNext();
                            }
                        }
                    }
                }
            }
        });
    }

    public void setOnViewPagerAction(OnViewPagerAction onViewPagerAction) {
        this.onViewPagerAction = onViewPagerAction;
    }

    public void setOnboardingType(int onboardingType) {
        this.onboardingType = onboardingType;
    }
}
