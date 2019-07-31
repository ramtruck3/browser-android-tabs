package org.chromium.chrome.browser.onboarding;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class BraveRewardsServiceReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        Intent mBraveRewardsServiceIntent = new Intent(context, BraveRewardsService.class);
        context.startService(mBraveRewardsServiceIntent);
    }
}