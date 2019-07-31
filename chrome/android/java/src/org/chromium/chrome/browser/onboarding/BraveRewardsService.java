package org.chromium.chrome.browser.onboarding;

import android.content.Intent;
import android.content.Context;
import android.app.Service;
import android.os.IBinder;
import android.util.Log;

import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.BraveRewardsNativeWorker;
import org.chromium.chrome.browser.BraveAdsNativeHelper;
import org.chromium.chrome.browser.BraveRewardsObserver;

public class BraveRewardsService extends Service implements BraveRewardsObserver{

    private Context context;
    private boolean isThreadRunning;
    private Thread backgroundThread;

    private BraveRewardsNativeWorker mBraveRewardsNativeWorker = BraveRewardsNativeWorker.getInstance();

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        mBraveRewardsNativeWorker.AddObserver(this);
        this.context = this;
        this.isThreadRunning = false;
        this.backgroundThread = new Thread(mBraveRewardsTask);
    }

    private Runnable mBraveRewardsTask = new Runnable() {
        @Override
        public void run() {
             mBraveRewardsNativeWorker.CreateWallet();
        }
    };

    @Override
    public void onDestroy() {
        this.isThreadRunning = false;
        if (mBraveRewardsNativeWorker != null) {
            mBraveRewardsNativeWorker.RemoveObserver(this);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if(!this.isThreadRunning) {
            this.isThreadRunning = true;
            this.backgroundThread.start();
        }

        return START_STICKY;
    }

    //interface BraveRewardsObserver
    @Override
    public void OnWalletInitialized(int error_code){
        if (BraveRewardsNativeWorker.WALLET_CREATED == error_code && OnboardingPrefManager.getInstance().isAdsAvailable()){
            // Enable ads
            BraveAdsNativeHelper.nativeSetAdsEnabled(Profile.getLastUsedProfile());
        }
        else {
            //TODO: handle wallet creation problem
        }
        stopSelf();
    };


    @Override
    public void OnWalletProperties(int error_code){};

    @Override
    public void OnPublisherInfo(int tabId){};

    @Override
    public void OnGetCurrentBalanceReport(String[] report){};

    @Override
    public void OnNotificationAdded(String id, int type, long timestamp, String[] args){};

    @Override
    public void OnNotificationsCount(int count){};

    @Override
    public void OnGetLatestNotification(String id, int type, long timestamp,
                                        String[] args){};

    @Override
    public void OnNotificationDeleted(String id){};

    @Override
    public void OnIsWalletCreated(boolean created){};

    @Override
    public void OnGetPendingContributionsTotal(double amount){};

    @Override
    public void OnGetRewardsMainEnabled(boolean enabled){};

    @Override
    public void OnGetAutoContributeProps(){};

    @Override
    public void OnGetReconcileStamp(long timestamp){};

    @Override
    public void OnRecurringDonationUpdated(){};

    @Override
    public void OnResetTheWholeState(boolean success){};

    @Override
    public void OnRewardsMainEnabled(boolean enabled){};

}