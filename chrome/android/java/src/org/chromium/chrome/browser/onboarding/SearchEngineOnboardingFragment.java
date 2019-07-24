package org.chromium.chrome.browser.onboarding;


import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Toast;

import org.chromium.chrome.browser.onboarding.Constants;
import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.browser.search_engines.TemplateUrl;
import org.chromium.chrome.browser.search_engines.TemplateUrlService;

import org.chromium.chrome.R;

import org.chromium.chrome.browser.onboarding.SearchEngineEnum;
import org.chromium.chrome.browser.onboarding.OnboardingPrefManager;

import java.util.Locale;
import java.util.Map;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;

import static org.chromium.chrome.browser.onboarding.Constants.dpToPx;

public class SearchEngineOnboardingFragment extends Fragment{

    private RadioGroup radioGroup;

    private Button btnSkip, btnNext;

    private OnViewPagerAction onViewPagerAction;

    public SearchEngineOnboardingFragment() {
        // Required empty public constructor
    }


    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        View root = inflater.inflate(R.layout.fragment_search_engine_onboarding, container, false);

        initializeViews(root);

        setActions();

        refreshData();

        return root;
    }

    private void refreshData() {
        TemplateUrlService templateUrlService = TemplateUrlService.getInstance();
        List<TemplateUrl> templateUrls = templateUrlService.getTemplateUrls();
        TemplateUrl defaultSearchEngineTemplateUrl = templateUrlService.getDefaultSearchEngineTemplateUrl();

        for(TemplateUrl templateUrl : templateUrls){

            SearchEngineEnum searchEngineEnum = Constants.defaultSearchEngineMap.get(templateUrl.getShortName());

            RadioButton rdBtn = new RadioButton(getActivity());
            rdBtn.setId(searchEngineEnum.getId());
            RadioGroup.LayoutParams params = new RadioGroup.LayoutParams(RadioGroup.LayoutParams.MATCH_PARENT, dpToPx(56, getActivity()));
            rdBtn.setLayoutParams(params);
            rdBtn.setTextSize(18);
            rdBtn.setButtonDrawable(null);
            rdBtn.setPadding(dpToPx(30, getActivity()), 0, 0, 0);
            rdBtn.setTextColor(getResources().getColor(R.color.black));
            rdBtn.setBackgroundDrawable(getResources().getDrawable(R.drawable.radiobutton_background));
            rdBtn.setText(searchEngineEnum.getTitle());
            rdBtn.setCompoundDrawablesWithIntrinsicBounds(getResources().getDrawable(searchEngineEnum.getIcon()), null, null, null);
            rdBtn.setCompoundDrawablePadding(dpToPx(16, getActivity()));
            radioGroup.addView(rdBtn);
        }

        radioGroup.check(Constants.defaultSearchEngineMap.get(defaultSearchEngineTemplateUrl.getShortName()).getId());
        radioGroup.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(RadioGroup radioGroup, int i) {
                View radioButton = radioGroup.findViewById(i);
                int index = radioGroup.indexOfChild(radioButton);
                searchEngineSelected(index, templateUrls);
            }
        });
    }

    public void setOnViewPagerAction(OnViewPagerAction onViewPagerAction) {
        this.onViewPagerAction = onViewPagerAction;
    }

    private void initializeViews(View root) {

        radioGroup = root.findViewById(R.id.radio_group);

        btnSkip = root.findViewById(R.id.btn_skip);
        btnNext = root.findViewById(R.id.btn_next);
    }

    private void setActions() {
        btnSkip.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onViewPagerAction.onSkip();
            }
        });

        btnNext.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onViewPagerAction.onNext();
            }
        });
    }

    private void searchEngineSelected(int position, List<TemplateUrl> templateUrls) {
        OnboardingPrefManager.selectedSearchEngine = templateUrls.get(position);
    }
}
