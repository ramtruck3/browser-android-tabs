// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test;

import android.content.Context;
import android.os.Build;
import android.support.test.InstrumentationRegistry;
import android.text.TextUtils;

import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.vr.ndk.base.DaydreamApi;
import com.google.vr.ndk.base.GvrApi;

import org.junit.rules.TestRule;
import org.junit.runners.model.InitializationError;

import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.StrictModeContext;
import org.chromium.base.test.BaseTestResult.PreTestHook;
import org.chromium.base.test.util.RestrictionSkipCheck;
import org.chromium.base.test.util.SkipCheck;
import org.chromium.chrome.browser.ChromeVersionInfo;
import org.chromium.chrome.browser.util.FeatureUtilities;
import org.chromium.chrome.test.util.ChromeRestriction;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.content_public.browser.test.ContentJUnit4ClassRunner;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.policy.test.annotations.Policies;

import java.util.List;
import java.util.concurrent.ExecutionException;

/**
 * A custom runner for //chrome JUnit4 tests.
 */
public class ChromeJUnit4ClassRunner extends ContentJUnit4ClassRunner {
    /**
     * Create a ChromeJUnit4ClassRunner to run {@code klass} and initialize values
     *
     * @throws InitializationError if the test class malformed
     */
    public ChromeJUnit4ClassRunner(final Class<?> klass) throws InitializationError {
        super(klass);
    }

    @Override
    protected List<SkipCheck> getSkipChecks() {
        return addToList(super.getSkipChecks(),
                new ChromeRestrictionSkipCheck(InstrumentationRegistry.getTargetContext()));
    }

    @Override
    protected List<PreTestHook> getPreTestHooks() {
        return addToList(super.getPreTestHooks(), Policies.getRegistrationHook());
    }

    @Override
    protected List<TestRule> getDefaultTestRules() {
        return addToList(super.getDefaultTestRules(), new Features.InstrumentationProcessor());
    }

    private static class ChromeRestrictionSkipCheck extends RestrictionSkipCheck {
        public ChromeRestrictionSkipCheck(Context targetContext) {
            super(targetContext);
        }

        private boolean isDaydreamReady() {
            // We normally check things like this through VrShellDelegate. However, with the
            // introduction of Dynamic Feature Modules (DFMs), we have tests that expect the VR
            // DFM to not be loaded. Using the normal approach (VrModuleProvider.getDelegate())
            // causes the DFM to be loaded, which we don't want done before the test starts. So,
            // access the Daydream API directly.
            return DaydreamApi.isDaydreamReadyPlatform(ContextUtils.getApplicationContext());
        }

        private boolean isDaydreamViewPaired() {
            if (!isDaydreamReady()) {
                return false;
            }
            // We need to ensure that the DaydreamApi instance is created on the main thread.
            DaydreamApi daydreamApi;
            try {
                daydreamApi = TestThreadUtils.runOnUiThreadBlocking(
                        () -> { return DaydreamApi.create(ContextUtils.getApplicationContext()); });
            } catch (ExecutionException e) {
                return false;
            }
            int viewerType;
            // Getting the current viewer type may result in a disk write in VrCore, so allow that
            // to prevent StrictMode errors.
            try (StrictModeContext smc = StrictModeContext.allowDiskWrites()) {
                viewerType = daydreamApi.getCurrentViewerType();
            }
            daydreamApi.close();
            return viewerType == GvrApi.ViewerType.DAYDREAM;
        }

        private boolean isOnStandaloneVrDevice() {
            return Build.DEVICE.equals("vega");
        }

        private boolean isVrSettingsServiceEnabled() {
            // We can't directly check whether the VR settings service is enabled since we don't
            // have permission to read the VrCore settings file. Instead, pass a flag.
            return CommandLine.getInstance().hasSwitch("vr-settings-service-enabled");
        }

        @Override
        protected boolean restrictionApplies(String restriction) {
            if (TextUtils.equals(
                        restriction, ChromeRestriction.RESTRICTION_TYPE_GOOGLE_PLAY_SERVICES)
                    && (ConnectionResult.SUCCESS
                               != GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(
                                          getTargetContext()))) {
                return true;
            }
            if (TextUtils.equals(restriction, ChromeRestriction.RESTRICTION_TYPE_OFFICIAL_BUILD)
                    && (!ChromeVersionInfo.isOfficialBuild())) {
                return true;
            }
            if (TextUtils.equals(restriction, ChromeRestriction.RESTRICTION_TYPE_DEVICE_DAYDREAM)
                    || TextUtils.equals(restriction,
                               ChromeRestriction.RESTRICTION_TYPE_DEVICE_NON_DAYDREAM)) {
                boolean isDaydream = isDaydreamReady();
                if (TextUtils.equals(
                            restriction, ChromeRestriction.RESTRICTION_TYPE_DEVICE_DAYDREAM)
                        && !isDaydream) {
                    return true;
                } else if (TextUtils.equals(restriction,
                                   ChromeRestriction.RESTRICTION_TYPE_DEVICE_NON_DAYDREAM)
                        && isDaydream) {
                    return true;
                }
            }
            if (TextUtils.equals(restriction, ChromeRestriction.RESTRICTION_TYPE_VIEWER_DAYDREAM)
                    || TextUtils.equals(restriction,
                               ChromeRestriction.RESTRICTION_TYPE_VIEWER_NON_DAYDREAM)) {
                boolean daydreamViewPaired = isDaydreamViewPaired();
                if (TextUtils.equals(
                            restriction, ChromeRestriction.RESTRICTION_TYPE_VIEWER_DAYDREAM)
                        && (!daydreamViewPaired || isOnStandaloneVrDevice())) {
                    return true;
                } else if (TextUtils.equals(restriction,
                                   ChromeRestriction.RESTRICTION_TYPE_VIEWER_NON_DAYDREAM)
                        && daydreamViewPaired) {
                    return true;
                }
            }
            if (TextUtils.equals(restriction, ChromeRestriction.RESTRICTION_TYPE_STANDALONE)) {
                return !isOnStandaloneVrDevice();
            }
            if (TextUtils.equals(restriction,
                        ChromeRestriction.RESTRICTION_TYPE_VIEWER_DAYDREAM_OR_STANDALONE)) {
                // Standalone devices are considered to have Daydream View paired.
                return !isDaydreamViewPaired();
            }
            if (TextUtils.equals(restriction, ChromeRestriction.RESTRICTION_TYPE_SVR)) {
                return isOnStandaloneVrDevice();
            }
            if (TextUtils.equals(
                        restriction, ChromeRestriction.RESTRICTION_TYPE_VR_SETTINGS_SERVICE)) {
                return !isVrSettingsServiceEnabled();
            }
            if (TextUtils.equals(restriction, ChromeRestriction.RESTRICTION_TYPE_REQUIRES_TOUCH)) {
                return FeatureUtilities.isNoTouchModeEnabled();
            }
            return false;
        }
    }
}
