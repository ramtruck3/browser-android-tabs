package org.chromium.chrome.browser.onboarding;

import org.chromium.chrome.R;

public enum SearchEngineEnum {
    GOOGLE(R.drawable.search_engine_google,R.string.google, R.string.search_google),
    DUCKDUCKGO(R.drawable.search_engine_duckduckgo,R.string.duckduckgo, R.string.search_duckduckgo),
    DUCKDUCKGOLITE(R.drawable.search_engine_duckduckgo_lite,R.string.duckduckgo_lite, R.string.search_duckduckgo_lite),
    QWANT(R.drawable.search_engine_qwant,R.string.qwant, R.string.search_qwant),
    BING(R.drawable.search_engine_bing,R.string.bing, R.string.search_bing),
    STARTPAGE(R.drawable.search_engine_startpage,R.string.start_page, R.string.search_start_page),
    YANDEX(R.drawable.search_engine_yandex,R.string.yandex, R.string.search_yandex);

    private int icon;
    private int title;
    private int id;

    SearchEngineEnum(int icon, int title,int id) {
        this.icon = icon;
        this.title = title;
        this.id = id;
    }

    public int getIcon() {
        return icon;
    }

    public int getTitle() {
        return title;
    }

    public int getId(){
        return id;
    }
}