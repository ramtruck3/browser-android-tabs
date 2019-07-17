package org.chromium.chrome.browser.onboarding;

import android.content.Context;
import android.os.Build;
import android.text.Html;
import android.text.Spanned;
import android.util.TypedValue;
import android.view.View;
import android.view.animation.AccelerateInterpolator;
import android.view.animation.AlphaAnimation;
import android.view.animation.Animation;

import java.util.HashMap;
import java.util.Map;

public class Constants {
    public static Map<Integer,SearchEngineEnum> germanySearchEngineMap = new HashMap<Integer, SearchEngineEnum>() {{
        put(0, SearchEngineEnum.QWANT);
        put(1, SearchEngineEnum.GOOGLE);
        put(2, SearchEngineEnum.DUCKDUCKGO);
        put(3, SearchEngineEnum.BING);
        put(4, SearchEngineEnum.STARTPAGE);
    }};

    public static Map<Integer,SearchEngineEnum> franceSearchEngineMap = new HashMap<Integer, SearchEngineEnum>() {{
        put(0, SearchEngineEnum.QWANT);
        put(1, SearchEngineEnum.GOOGLE);
        put(2, SearchEngineEnum.DUCKDUCKGO);
        put(3, SearchEngineEnum.BING);
        put(4, SearchEngineEnum.STARTPAGE);
    }};


    public static Map<Integer,SearchEngineEnum> defaultSearchEngineMap = new HashMap<Integer, SearchEngineEnum>() {{
        put(0, SearchEngineEnum.GOOGLE);
        put(1, SearchEngineEnum.DUCKDUCKGO);
        put(2, SearchEngineEnum.QWANT);
        put(3, SearchEngineEnum.BING);
        put(4, SearchEngineEnum.STARTPAGE);
    }};

    public static int dpToPx(float dp, Context context) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp, context.getResources().getDisplayMetrics());
    }

    public static boolean isNotification;

    public static void fadeOutView(final View view) {
        Animation fadeOut = new AlphaAnimation(1, 0);
        fadeOut.setInterpolator(new AccelerateInterpolator());
        fadeOut.setDuration(1000);

        fadeOut.setAnimationListener(new Animation.AnimationListener() {
            @Override
            public void onAnimationEnd(Animation animation) {
                view.setVisibility(View.GONE);
            }

            @Override
            public void onAnimationRepeat(Animation animation) {
            }

            @Override
            public void onAnimationStart(Animation animation) {
            }
        });

        view.startAnimation(fadeOut);
    }

    public static void fadeInView(final View view) {
        Animation fadeIn = new AlphaAnimation(0, 1);
        fadeIn.setInterpolator(new AccelerateInterpolator());
        fadeIn.setDuration(1000);

        fadeIn.setAnimationListener(new Animation.AnimationListener() {
            @Override
            public void onAnimationEnd(Animation animation) {
                view.setVisibility(View.VISIBLE);
            }

            @Override
            public void onAnimationRepeat(Animation animation) {
            }

            @Override
            public void onAnimationStart(Animation animation) {
            }
        });

        view.startAnimation(fadeIn);
    }


    // Use it from BraveRewardsHelper
    public static Spanned spannedFromHtmlString(String string) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            return Html.fromHtml(string, Html.FROM_HTML_MODE_LEGACY);
        } else {
            return Html.fromHtml(string);
        }
    }
}
