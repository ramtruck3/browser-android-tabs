// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/session_cleanup_cookie_store.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/task/post_task.h"
#include "base/task/thread_pool/thread_pool.h"
#include "base/test/scoped_task_environment.h"
#include "base/time/time.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace network {
namespace {

using CanonicalCookieVector =
    std::vector<std::unique_ptr<net::CanonicalCookie>>;

const base::FilePath::CharType kTestCookiesFilename[] =
    FILE_PATH_LITERAL("Cookies");

class SessionCleanupCookieStoreTest : public testing::Test {
 public:
  SessionCleanupCookieStoreTest() {}

  void OnLoaded(base::RunLoop* run_loop,
                CanonicalCookieVector* cookies_out,
                CanonicalCookieVector cookies) {
    cookies_out->swap(cookies);
    run_loop->Quit();
  }

  CanonicalCookieVector Load() {
    base::RunLoop run_loop;
    CanonicalCookieVector cookies;
    store_->Load(
        base::BindRepeating(&SessionCleanupCookieStoreTest::OnLoaded,
                            base::Unretained(this), &run_loop, &cookies),
        net_log_.bound());
    run_loop.Run();
    return cookies;
  }

 protected:
  CanonicalCookieVector CreateAndLoad() {
    auto sqlite_store = base::MakeRefCounted<net::SQLitePersistentCookieStore>(
        temp_dir_.GetPath().Append(kTestCookiesFilename),
        base::CreateSequencedTaskRunnerWithTraits({base::MayBlock()}),
        background_task_runner_, true, nullptr);
    store_ =
        base::MakeRefCounted<SessionCleanupCookieStore>(sqlite_store.get());
    return Load();
  }

  // Adds a persistent cookie to store_.
  void AddCookie(const std::string& name,
                 const std::string& value,
                 const std::string& domain,
                 const std::string& path,
                 base::Time creation) {
    store_->AddCookie(net::CanonicalCookie(name, value, domain, path, creation,
                                           creation, base::Time(), false, false,
                                           net::CookieSameSite::NO_RESTRICTION,
                                           net::COOKIE_PRIORITY_DEFAULT));
  }

  void DestroyStore() {
    store_ = nullptr;
    // Ensure that |store_|'s destructor has run by flushing ThreadPool.
    base::ThreadPool::GetInstance()->FlushForTesting();
  }

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  void TearDown() override { DestroyStore(); }

  base::test::ScopedTaskEnvironment scoped_task_environment_;
  const scoped_refptr<base::SequencedTaskRunner> background_task_runner_ =
      base::CreateSequencedTaskRunnerWithTraits({base::MayBlock()});
  base::ScopedTempDir temp_dir_;
  scoped_refptr<SessionCleanupCookieStore> store_;
  net::BoundTestNetLog net_log_;
};

TEST_F(SessionCleanupCookieStoreTest, TestPersistence) {
  CanonicalCookieVector cookies = CreateAndLoad();
  ASSERT_EQ(0u, cookies.size());

  base::Time t = base::Time::Now();
  AddCookie("A", "B", "foo.com", "/", t);
  t += base::TimeDelta::FromDays(10);
  AddCookie("A", "B", "persistent.com", "/", t);

  // Replace the store, which forces the current store to flush data to
  // disk. Then, after reloading the store, confirm that the data was flushed by
  // making sure it loads successfully.  This ensures that all pending commits
  // are made to the store before allowing it to be closed.
  DestroyStore();

  // Reload and test for persistence.
  cookies = CreateAndLoad();
  EXPECT_EQ(2u, cookies.size());
  bool found_foo_cookie = false;
  bool found_persistent_cookie = false;
  for (const auto& cookie : cookies) {
    if (cookie->Domain() == "foo.com")
      found_foo_cookie = true;
    else if (cookie->Domain() == "persistent.com")
      found_persistent_cookie = true;
  }
  EXPECT_TRUE(found_foo_cookie);
  EXPECT_TRUE(found_persistent_cookie);

  // Now delete the cookies and check persistence again.
  store_->DeleteCookie(*cookies[0]);
  store_->DeleteCookie(*cookies[1]);
  DestroyStore();

  // Reload and check if the cookies have been removed.
  cookies = CreateAndLoad();
  EXPECT_EQ(0u, cookies.size());
  cookies.clear();
}

TEST_F(SessionCleanupCookieStoreTest, TestNetLogIncludeCookies) {
  CanonicalCookieVector cookies = CreateAndLoad();
  base::Time t = base::Time::Now();
  AddCookie("A", "B", "nonpersistent.com", "/", t);

  // Cookies from "nonpersistent.com" should be deleted.
  store_->DeleteSessionCookies(
      base::BindRepeating([](const std::string& domain, bool is_https) {
        return domain == "nonpersistent.com";
      }));
  DestroyStore();

  net::TestNetLogEntry::List entries;
  net_log_.GetEntries(&entries);
  size_t pos = net::ExpectLogContainsSomewhere(
      entries, 0, net::NetLogEventType::COOKIE_PERSISTENT_STORE_ORIGIN_FILTERED,
      net::NetLogEventPhase::NONE);
  std::string cookie_origin;
  bool cookie_is_https = true;
  EXPECT_TRUE(entries[pos].GetStringValue("origin", &cookie_origin));
  EXPECT_TRUE(entries[pos].GetBooleanValue("is_https", &cookie_is_https));
  EXPECT_EQ("nonpersistent.com", cookie_origin);
  EXPECT_EQ(false, cookie_is_https);
  pos = net::ExpectLogContainsSomewhere(
      entries, pos, net::NetLogEventType::COOKIE_PERSISTENT_STORE_CLOSED,
      net::NetLogEventPhase::NONE);
  std::string event_type;
  EXPECT_TRUE(entries[pos].GetStringValue("type", &event_type));
  EXPECT_EQ("SessionCleanupCookieStore", event_type);
}

TEST_F(SessionCleanupCookieStoreTest, TestNetLogDoNotIncludeCookies) {
  CanonicalCookieVector cookies = CreateAndLoad();
  base::Time t = base::Time::Now();
  AddCookie("A", "B", "nonpersistent.com", "/", t);

  net_log_.SetCaptureMode(net::NetLogCaptureMode::Default());
  // Cookies from "nonpersistent.com" should be deleted.
  store_->DeleteSessionCookies(
      base::BindRepeating([](const std::string& domain, bool is_https) {
        return domain == "nonpersistent.com";
      }));
  DestroyStore();

  net::TestNetLogEntry::List entries;
  net_log_.GetEntries(&entries);
  size_t pos = net::ExpectLogContainsSomewhere(
      entries, 0, net::NetLogEventType::COOKIE_PERSISTENT_STORE_ORIGIN_FILTERED,
      net::NetLogEventPhase::NONE);
  std::string cookie_origin;
  bool cookie_is_https = true;
  EXPECT_FALSE(entries[pos].GetStringValue("origin", &cookie_origin));
  EXPECT_FALSE(entries[pos].GetBooleanValue("is_https", &cookie_is_https));
  pos = net::ExpectLogContainsSomewhere(
      entries, pos, net::NetLogEventType::COOKIE_PERSISTENT_STORE_CLOSED,
      net::NetLogEventPhase::NONE);
  std::string event_type;
  EXPECT_TRUE(entries[pos].GetStringValue("type", &event_type));
  EXPECT_EQ("SessionCleanupCookieStore", event_type);
}

TEST_F(SessionCleanupCookieStoreTest, TestDeleteSessionCookies) {
  CanonicalCookieVector cookies = CreateAndLoad();
  ASSERT_EQ(0u, cookies.size());

  base::Time t = base::Time::Now();
  AddCookie("A", "B", "foo.com", "/", t);
  t += base::TimeDelta::FromDays(10);
  AddCookie("A", "B", "persistent.com", "/", t);
  t += base::TimeDelta::FromDays(10);
  AddCookie("A", "B", "nonpersistent.com", "/", t);

  // Replace the store, which forces the current store to flush data to
  // disk. Then, after reloading the store, confirm that the data was flushed by
  // making sure it loads successfully.  This ensures that all pending commits
  // are made to the store before allowing it to be closed.
  DestroyStore();

  // Reload and test for persistence.
  cookies = CreateAndLoad();
  EXPECT_EQ(3u, cookies.size());

  t += base::TimeDelta::FromDays(10);
  AddCookie("A", "B", "nonpersistent.com", "/second", t);

  // Cookies from "nonpersistent.com" should be deleted.
  store_->DeleteSessionCookies(
      base::BindRepeating([](const std::string& domain, bool is_https) {
        return domain == "nonpersistent.com";
      }));
  scoped_task_environment_.RunUntilIdle();
  DestroyStore();
  cookies = CreateAndLoad();

  EXPECT_EQ(2u, cookies.size());
  for (const auto& cookie : cookies) {
    EXPECT_NE("nonpersistent.com", cookie->Domain());
  }
  cookies.clear();
}

TEST_F(SessionCleanupCookieStoreTest, ForceKeepSessionState) {
  CanonicalCookieVector cookies = CreateAndLoad();
  ASSERT_EQ(0u, cookies.size());

  base::Time t = base::Time::Now();
  AddCookie("A", "B", "foo.com", "/", t);

  // Recreate |store_|, and call DeleteSessionCookies with a function that that
  // makes "nonpersistent.com" session only, but then instruct the store to
  // forcibly keep all cookies.

  // Reload and test for persistence
  DestroyStore();
  cookies = CreateAndLoad();
  EXPECT_EQ(1u, cookies.size());

  t += base::TimeDelta::FromDays(10);
  AddCookie("A", "B", "persistent.com", "/", t);
  t += base::TimeDelta::FromDays(10);
  AddCookie("A", "B", "nonpersistent.com", "/", t);

  store_->SetForceKeepSessionState();
  // Cookies from "nonpersistent.com" should NOT be deleted.
  store_->DeleteSessionCookies(
      base::BindRepeating([](const std::string& domain, bool is_https) {
        return domain == "nonpersistent.com";
      }));
  scoped_task_environment_.RunUntilIdle();
  DestroyStore();
  cookies = CreateAndLoad();

  EXPECT_EQ(3u, cookies.size());
  cookies.clear();
}

}  // namespace
}  // namespace network
