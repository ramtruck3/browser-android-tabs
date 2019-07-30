/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BLOCKERS_WORKER_H_
#define BLOCKERS_WORKER_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "RecentlyUsedCache.h"
#include "url/gurl.h"
#include "content/public/common/referrer.h"

class GURL;

namespace adblock {
    class Engine;
}

namespace leveldb {
    class DB;
}

namespace net {
namespace blockers {

struct HTTPSE_REDIRECTS_COUNT_ST {
public:
    HTTPSE_REDIRECTS_COUNT_ST(uint64_t request_identifier, unsigned int redirects):
      request_identifier_(request_identifier),
      redirects_(redirects) {
    }

    uint64_t request_identifier_;
    unsigned int redirects_;
};

class BlockersWorker {
public:
    BlockersWorker();
    ~BlockersWorker();

    bool shouldAdBlockUrl(const std::string& tab_url, const std::string& url, unsigned int resource_type,
      bool isAdBlockRegionalEnabled);
    std::string getHTTPSURLFromCacheOnly(const GURL* url, const uint64_t &request_id);
    std::string getHTTPSURL(const GURL* url, const uint64_t &request_id);
    bool isAdBlockerInitialized();
    bool isAdBlockerRegionalInitialized();

    bool InitAdBlock();
    bool InitAdBlockRegional();
    bool InitHTTPSE();

    static bool ShouldSetReferrer(bool allow_referrers, bool shields_up,
        const GURL& original_referrer, const GURL& tab_origin,
        const GURL& target_url, const GURL& new_referrer_url,
        network::mojom::ReferrerPolicy policy, content::Referrer *output_referrer);
    static bool IsWhitelistedReferrer(const GURL& firstPartyOrigin,
                           const GURL& subresourceUrl);
private:
    std::string applyHTTPSRule(const std::string& originalUrl, const std::string& rule);
    std::vector<std::string> getTPThirdPartyHosts(const std::string& base_host);

    bool GetData(const std::string& version, const std::string& fileName, std::vector<unsigned char>& buffer, bool only_file_name = false);
    bool GetBufferData(const std::string& version, const std::string& fileName, std::vector<unsigned char>& buffer);

    std::string correcttoRuleToRE2Engine(const std::string& to);

    void addHTTPSEUrlToRedirectList(const uint64_t &request_id);
    bool shouldHTTPSERedirect(const uint64_t &request_id);

    void set_adblock_initialized();
    void set_adblock_regional_initialized();

    bool GetRecentlyUsedCacheValue(const std::string& key, std::string& value);
    void SetRecentlyUsedCacheValue(const std::string& key, const std::string& value);

    std::vector<unsigned char> adblock_buffer_;
    std::vector<std::vector<unsigned char>> adblock_regional_buffer_;
    leveldb::DB* level_db_;
    // We use that to cache httpse requests (do not use it directly, only covered with httpse_recently_used_cache_mutex_)
    RecentlyUsedCache<std::string> recently_used_cache_;
    adblock::Engine* adblock_parser_;
    std::vector<adblock::Engine*> adblock_regional_parsers_;

    std::vector<HTTPSE_REDIRECTS_COUNT_ST> httpse_urls_redirects_count_;

    bool adblock_initialized_;
    bool adblock_regional_initialized_;

    std::mutex httpse_init_mutex_;
    std::mutex adblock_init_mutex_;
    std::mutex adblock_regional_init_mutex_;
    std::mutex httpse_get_urls_redirects_count_mutex_;
    std::mutex httpse_recently_used_cache_mutex_;
    std::mutex adblock_initialized_mutex_;
    std::mutex adblock_regional_initialized_mutex_;
};

}  // namespace blockers
}  // namespace net

#endif
