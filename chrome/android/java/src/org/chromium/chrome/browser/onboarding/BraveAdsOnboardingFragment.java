package org.chromium.chrome.browser.onboarding;


import android.app.Fragment;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.CountDownTimer;
import android.os.Handler;
import android.support.v4.app.NotificationCompat;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.chromium.chrome.browser.onboarding.Constants;
import org.chromium.chrome.browser.onboarding.OnViewPagerAction;
import org.chromium.chrome.R;

import static org.chromium.chrome.browser.onboarding.Constants.fadeInView;
import static org.chromium.chrome.browser.onboarding.Constants.fadeOutView;

public class BraveAdsOnboardingFragment extends Fragment {

    private static final String CHANNEL_ID = "channel01";
    private static final int NOTIFICATION_ID = 0;
    private static final String BRAVE_MY_FIRST_AD_URL = "http://www.brave.com/my-first-ad/";

    private OnViewPagerAction onViewPagerAction;

    private int progress;
    private int endTime = 3;

    private TextView tvTitle, tvTimer;

    private ProgressBar progressBarView;

    private CountDownTimer countDownTimer;
    private LinearLayout countDownLayout, actionLayout;

    private Button btnStartBrowsing, btnDidntSeeAd;

    public BraveAdsOnboardingFragment() {
        // Required empty public constructor
    }


    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        View root = inflater.inflate(R.layout.fragment_brave_ads_onboarding, container, false);

        initializeViews(root);

        setActions();

        return root;
    }

    @Override
    public void setUserVisibleHint(boolean isVisibleToUser) {
        super.setUserVisibleHint(isVisibleToUser);
        if (isVisibleToUser) {
            countDownLayout.setVisibility(View.VISIBLE);
            tvTitle.setVisibility(View.VISIBLE);
            actionLayout.setVisibility(View.GONE);
            start_countdown();
            Constants.isNotification = true;
        }
    }

    private void setActions() {
        btnStartBrowsing.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onViewPagerAction.onStartBrowsing();
            }
        });

        btnDidntSeeAd.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                onViewPagerAction.onDidntSeeAd();
            }
        });
    }

    private void initializeViews(View root) {
        tvTitle = root.findViewById(R.id.section_title);

        countDownLayout = root.findViewById(R.id.count_down_layout);
        actionLayout = root.findViewById(R.id.action_layout);

        tvTimer = root.findViewById(R.id.tv_timer);

        btnStartBrowsing = root.findViewById(R.id.btn_start_browsing);
        btnDidntSeeAd = root.findViewById(R.id.btn_didnt_see_ad);

        progressBarView = root.findViewById(R.id.view_progress_bar);
    }

    public void setOnViewPagerAction(OnViewPagerAction onViewPagerAction) {
        this.onViewPagerAction = onViewPagerAction;
    }

    private void start_countdown() {

        if (countDownTimer != null)
            countDownTimer.cancel();

        progress = 0;

        countDownTimer = new CountDownTimer(endTime * 1000, 100) {
            @Override
            public void onTick(long millisUntilFinished) {
                setProgress(progress, endTime);
                progress = progress + 100;
                tvTimer.setText(String.valueOf((millisUntilFinished / 1000)+1));
            }

            @Override
            public void onFinish() {
                setProgress(progress, endTime);
                tvTimer.setText("0");

                sendLocalNotification();

                new Handler().postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        fadeOutView(countDownLayout);

                        fadeInView(actionLayout);

                        Constants.isNotification = false;

                    }
                }, 1000);
            }
        };
        countDownTimer.start();
    }

    private void sendLocalNotification() {

        NotificationManager notificationManager =  (NotificationManager) getActivity().getSystemService(Context.NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            CharSequence name = "Brave Browser";
            String description = "Notification channel for Brave Browser";
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, name,
                    NotificationManager.IMPORTANCE_HIGH);
            channel.setDescription(description);
            notificationManager.createNotificationChannel(channel);
        }

        Intent notificationIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(BRAVE_MY_FIRST_AD_URL));

        PendingIntent pendingIntent = PendingIntent.getActivities(getActivity(), 0, new Intent[]{notificationIntent}, PendingIntent.FLAG_UPDATE_CURRENT);

        NotificationCompat.Builder notificationBuilder = new NotificationCompat.Builder(getActivity(),CHANNEL_ID)
                .setSmallIcon(R.mipmap.app_icon)
                .setContentTitle(getResources().getString(R.string.this_is_your_first_ad))
                .setContentText(getResources().getString(R.string.tap_here_to_learn_more))
                .setDefaults(Notification.DEFAULT_ALL)
                .setPriority(Notification.PRIORITY_HIGH)   // heads-up
                .setContentIntent(pendingIntent)
                .setAutoCancel(true);

        notificationManager.notify(NOTIFICATION_ID, notificationBuilder.build());

    }

    private void setProgress(int startTime, int endTime) {
        progressBarView.setMax(endTime * 1000);
        progressBarView.setSecondaryProgress(endTime * 1000);
        progressBarView.setProgress(startTime);
    }
}
