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

import android.support.annotation.NonNull;

import org.chromium.chrome.browser.onboarding.Constants;
import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;
import org.chromium.chrome.R;

import static org.chromium.chrome.browser.onboarding.Constants.fadeInView;
import static org.chromium.chrome.browser.onboarding.Constants.fadeOutView;

public class BraveRewardsOnboardingFragment extends Fragment implements View.OnTouchListener {

    private OnViewPagerAction onViewPagerAction;

    private ImageView bgImage;

    private TextView tvTitle, tvText, tvAgree;

    private CheckBox chkAgreeTerms;

    private LinearLayout termAndAgreeLayout;

    private Button btnSkip, btnNext;
    private boolean isAgree;

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

        String braveRewardsText = "<b>" + getResources().getString(R.string.earn_tokens) + "</b> " + getResources().getString(R.string.brave_rewards_onboarding_text);
        final Spanned textToInsert = Constants.spannedFromHtmlString(braveRewardsText);
        tvText.setText(textToInsert);

        String termsText = getResources().getString(R.string.terms_text) + "<br/>" + getResources().getString(R.string.terms_of_service)+ ".";
        Spanned textToAgree = Constants.spannedFromHtmlString(termsText);
        SpannableString ss = new SpannableString(textToAgree.toString());

        ClickableSpan clickableSpan = new ClickableSpan() {
            @Override
            public void onClick(@NonNull View textView) {
                Intent webViewIntent = new Intent(getActivity(), OnboardingWebviewActivity.class);
                startActivity(webViewIntent);
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
                if (isAgree) {
                    fadeOutView(termAndAgreeLayout);
                    fadeOutView(tvTitle);
                    fadeOutView(tvText);

                    chkAgreeTerms.setChecked(false);

                    btnSkip.setText(getResources().getString(R.string.skip));
                    btnNext.setText(getResources().getString(R.string.next));
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
        });

        btnNext.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {

                if (!isAgree) {
                    fadeOutView(tvTitle);
                    tvText.setVisibility(View.GONE);

                    btnSkip.setText(getResources().getString(android.R.string.cancel));
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
                        if (!Constants.isAdsAvailable()) {
                            OnboardingPrefManager.getInstance().setPrefOnboardingEnabled(false);
                            getActivity().finish();
                        } else {
                            onViewPagerAction.onNext();
                        }
                    }
                }
            }
        });
    }

    public void setOnViewPagerAction(OnViewPagerAction onViewPagerAction) {
        this.onViewPagerAction = onViewPagerAction;
    }
}
