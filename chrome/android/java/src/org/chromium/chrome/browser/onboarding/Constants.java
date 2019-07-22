package org.chromium.chrome.browser.onboarding;

import android.content.Context;
import android.os.Build;
import android.text.Html;
import android.text.Spanned;
import android.util.TypedValue;

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


    // Use it from BraveRewardsHelper
    public static Spanned spannedFromHtmlString(String string) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            return Html.fromHtml(string, Html.FROM_HTML_MODE_LEGACY);
        } else {
            return Html.fromHtml(string);
        }
    }
}
