package org.chromium.chrome.browser.onboarding;

import android.content.Context;
import android.os.Build;
import android.text.Html;
import android.text.Spanned;
import android.util.TypedValue;

import java.util.HashMap;
import java.util.Map;

public class Constants {
    public static Map<String,SearchEngineEnum> defaultSearchEngineMap = new HashMap<String, SearchEngineEnum>() {{
        put("Google", SearchEngineEnum.GOOGLE);
        put("DuckDuckGo", SearchEngineEnum.DUCKDUCKGO);
        put("DuckDuckGo Light", SearchEngineEnum.DUCKDUCKGOLIGHT);
        put("Qwant", SearchEngineEnum.QWANT);
        put("Bing", SearchEngineEnum.BING);
        put("StartPage", SearchEngineEnum.STARTPAGE);
        put("Yandex", SearchEngineEnum.YANDEX);
    }};
}
