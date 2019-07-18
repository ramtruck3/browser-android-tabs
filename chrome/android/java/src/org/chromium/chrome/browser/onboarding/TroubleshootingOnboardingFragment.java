package org.chromium.chrome.browser.onboarding;

import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;

import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.R;

public class TroubleshootingOnboardingFragment extends Fragment {

    private OnViewPagerAction onViewPagerAction;

    private Button btnStartBrowsing;

    public TroubleshootingOnboardingFragment() {
        // Required empty public constructor
    }


    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        View root =  inflater.inflate(R.layout.fragment_troubleshooting_onboarding, container, false);

        initializeViews(root);

        setActions();

        return root;
    }

    private void setActions() {
        btnStartBrowsing.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onViewPagerAction.onStartBrowsing();
            }
        });
    }

    private void initializeViews(View root) {
        btnStartBrowsing = root.findViewById(R.id.btn_start_browsing);
    }

    public void setOnViewPagerAction(OnViewPagerAction onViewPagerAction) {
        this.onViewPagerAction = onViewPagerAction;
    }
}
