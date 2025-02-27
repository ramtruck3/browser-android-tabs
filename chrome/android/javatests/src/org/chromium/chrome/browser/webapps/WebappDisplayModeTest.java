// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.webapps;

import android.graphics.Color;
import android.net.Uri;
import android.support.test.filters.MediumTest;
import android.support.test.filters.SmallTest;
import android.view.View;
import android.widget.TextView;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.DisabledTest;
import org.chromium.base.test.util.Feature;
import org.chromium.blink_public.platform.WebDisplayMode;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.ShortcutHelper;
import org.chromium.chrome.browser.omnibox.UrlBar;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.browser.WebappTestPage;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.test.util.JavaScriptUtils;
import org.chromium.content_public.browser.test.util.TouchCommon;

import java.util.concurrent.TimeoutException;

/**
 * Test for various Display Modes of Web Apps.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
public class WebappDisplayModeTest {
    private static final String WEB_APP_PAGE_TITLE = "Web app banner test page";

    private static final String WEB_APP_PATH = "/chrome/test/data/banners/manifest_test_page.html";

    @Rule
    public final WebappActivityTestRule mActivityTestRule = new WebappActivityTestRule();

    @Test
    @SmallTest
    @Feature({"Webapps"})
    public void testStandalone() throws Exception {
        WebappActivity activity = startActivity(WebDisplayMode.STANDALONE, "");

        Assert.assertFalse(activity.getToolbarManager().getToolbarLayoutForTesting().isShown());
        Assert.assertFalse(isFullscreen(activity));
    }

    @Test
    //@SmallTest
    //@Feature({"Webapps"})
    @DisabledTest(message = "crbug.com/793133")
    public void testFullScreen() throws Exception {
        WebappActivity activity = startActivity(WebDisplayMode.FULLSCREEN, "");

        Assert.assertFalse(activity.getToolbarManager().getToolbarLayoutForTesting().isShown());
        Assert.assertTrue(isFullscreen(activity));
    }

    @Test
    @MediumTest
    @Feature({"Webapps"})
    public void testFullScreenInFullscreen() throws Exception {
        WebappActivity activity = startActivity(WebDisplayMode.FULLSCREEN, "fullscreen_on_click");

        Assert.assertFalse(activity.getToolbarManager().getToolbarLayoutForTesting().isShown());
        Assert.assertTrue(isFullscreen(activity));

        WebContents contents = activity.getActivityTab().getWebContents();

        TouchCommon.singleClickView(activity.getActivityTab().getContentView());
        // Poll because clicking races with evaluating js evaluation.
        CriteriaHelper.pollInstrumentationThread(
                () -> getJavascriptResult(contents, "isBodyFullscreen()").equals("true"));
        Assert.assertTrue(isFullscreen(activity));

        TouchCommon.singleClickView(activity.getActivityTab().getContentView());
        CriteriaHelper.pollInstrumentationThread(
                () -> getJavascriptResult(contents, "isBodyFullscreen()").equals("false"));
        Assert.assertTrue(isFullscreen(activity));
    }

    @Test
    //@SmallTest
    //@Feature({"Webapps"})
    @DisabledTest(message = "crbug.com/793133")
    public void testMinimalUi() throws Exception {
        WebappActivity activity = startActivity(WebDisplayMode.MINIMAL_UI, "");

        Assert.assertFalse(isFullscreen(activity));
        Assert.assertTrue(activity.getToolbarManager().getToolbarLayoutForTesting().isShown());

        Assert.assertEquals(Color.CYAN, activity.getToolbarManager().getPrimaryColor());
        Assert.assertEquals("Web App title should be displayed on the title bar",
                WEB_APP_PAGE_TITLE, ((TextView) activity.findViewById(R.id.title_bar)).getText());
        Assert.assertEquals("URL Bar should display URL authority",
                Uri.parse(mActivityTestRule.getTestServer().getURL(WEB_APP_PATH)).getAuthority(),
                ((UrlBar) activity.findViewById(R.id.url_bar)).getText().toString());
        Assert.assertEquals("CCT Close button should not be visible", View.GONE,
                activity.findViewById(R.id.close_button).getVisibility());
    }

    private String getJavascriptResult(WebContents webContents, String js) {
        try {
            return JavaScriptUtils.executeJavaScriptAndWaitForResult(webContents, js);
        } catch (InterruptedException | TimeoutException e) {
            Assert.fail("Fatal interruption or timeout running JavaScript '" + js
                    + "': " + e.toString());
            return "";
        }
    }

    private WebappActivity startActivity(@WebDisplayMode int displayMode, String action)
            throws Exception {
        String url = WebappTestPage.getServiceWorkerUrlWithAction(
                mActivityTestRule.getTestServer(), action);
        mActivityTestRule.startWebappActivity(
                mActivityTestRule.createIntent()
                        .putExtra(ShortcutHelper.EXTRA_URL, url)
                        .putExtra(ShortcutHelper.EXTRA_DISPLAY_MODE, displayMode)
                        .putExtra(ShortcutHelper.EXTRA_THEME_COLOR, (long) Color.CYAN));

        mActivityTestRule.waitUntilSplashscreenHides();
        mActivityTestRule.waitUntilIdle();

        return mActivityTestRule.getActivity();
    }

    private static boolean isFullscreen(WebappActivity activity) {
        int systemUiVisibility = activity.getWindow().getDecorView().getSystemUiVisibility();
        return (systemUiVisibility & WebappActivity.IMMERSIVE_MODE_UI_FLAGS)
                == WebappActivity.IMMERSIVE_MODE_UI_FLAGS;
    }
}
