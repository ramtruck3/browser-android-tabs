package org.chromium.chrome.browser.onboarding;


import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.RadioButton;
import android.widget.RadioGroup;

import org.chromium.chrome.browser.onboarding.Constants;
import org.chromium.chrome.browser.onboarding.OnViewPagerAction;

import org.chromium.chrome.R;

import org.chromium.chrome.browser.onboarding.SearchEngineEnum;

import java.util.Locale;
import java.util.Map;

import static org.chromium.chrome.browser.onboarding.Constants.dpToPx;

public class SearchEngineOnboardingFragment extends Fragment {

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

        return root;
    }

    public void setOnViewPagerAction(OnViewPagerAction onViewPagerAction) {
        this.onViewPagerAction = onViewPagerAction;
    }

    private void initializeViews(View root) {

        RadioGroup radioGroup = root.findViewById(R.id.radio_group);

        btnSkip = root.findViewById(R.id.btn_skip);
        btnNext = root.findViewById(R.id.btn_next);

        Locale locale = getResources().getConfiguration().locale;
        Map<Integer, SearchEngineEnum> searchEngineEnumMap;
        if (locale.equals(Locale.GERMANY)) {
            searchEngineEnumMap = Constants.franceSearchEngineMap;
        } else if (locale.equals(Locale.FRANCE)) {
            searchEngineEnumMap = Constants.germanySearchEngineMap;
        } else {
            searchEngineEnumMap = Constants.defaultSearchEngineMap;
        }

        for (SearchEngineEnum searchEngineEnum : searchEngineEnumMap.values()) {
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

        radioGroup.check(((SearchEngineEnum) searchEngineEnumMap.values().toArray()[0]).getId());
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
}
