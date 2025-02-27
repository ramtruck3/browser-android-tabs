// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/download/download_manager_service.h"

#include <memory>

#include "base/android/callback_android.h"
#include "base/android/jni_string.h"
#include "base/android/path_utils.h"
#include "base/bind.h"
#include "base/location.h"
#include "base/metrics/field_trial_params.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/android/chrome_feature_list.h"
#include "chrome/browser/android/download/download_controller.h"
#include "chrome/browser/android/download/download_utils.h"
#include "chrome/browser/android/download/service/download_task_scheduler.h"
#include "chrome/browser/android/feature_utilities.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/offline_item_utils.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/download/network/android/network_status_listener_android.h"
#include "components/download/public/common/auto_resumption_handler.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_item_impl.h"
#include "components/download/public/common/download_url_loader_factory_getter_impl.h"
#include "components/download/public/task/task_manager_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_request_utils.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/notification_service.h"
#include "jni/DownloadInfo_jni.h"
#include "jni/DownloadItem_jni.h"
#include "jni/DownloadManagerService_jni.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
namespace {

// The remaining time for a download item if it cannot be calculated.
constexpr int64_t kUnknownRemainingTime = -1;

// Finch flag for controlling auto resumption limit.
int kDefaultAutoResumptionLimit = 5;
const char kAutoResumptionLimitParamName[] = "AutoResumptionLimit";

bool ShouldShowDownloadItem(download::DownloadItem* item) {
  return !item->IsTemporary() && !item->IsTransient();
}

ScopedJavaLocalRef<jobject> JNI_DownloadManagerService_CreateJavaDownloadItem(
    JNIEnv* env,
    download::DownloadItem* item) {
  DCHECK(!item->IsTransient());
  return Java_DownloadItem_createDownloadItem(
      env, DownloadManagerService::CreateJavaDownloadInfo(env, item),
      item->GetStartTime().ToJavaTime(), item->GetEndTime().ToJavaTime(),
      item->GetFileExternallyRemoved());
}

// For download that doesn't require full browser process, origin security check
// is skipped as the download is either resumption or from a secure source.
bool IgnoreOriginSecurityCheck(const GURL& url) {
  return true;
}

void RenameItemCallback(
    const base::android::ScopedJavaGlobalRef<jobject> j_callback,
    download::DownloadItem::DownloadRenameResult result) {
  base::android::RunIntCallbackAndroid(
      j_callback,
      static_cast<int>(
          OfflineItemUtils::ConvertDownloadRenameResultToRenameResult(result)));
}
}  // namespace

// static
void DownloadManagerService::CreateAutoResumptionHandler() {
  auto network_listener =
      std::make_unique<download::NetworkStatusListenerAndroid>();
  auto task_scheduler =
      std::make_unique<download::android::DownloadTaskScheduler>();
  auto task_manager =
      std::make_unique<download::TaskManagerImpl>(std::move(task_scheduler));
  auto config = std::make_unique<download::AutoResumptionHandler::Config>();
  config->auto_resumption_size_limit =
      DownloadUtils::GetAutoResumptionSizeLimit();
  config->is_auto_resumption_enabled_in_native =
      chrome::android::IsDownloadAutoResumptionEnabledInNative();
  download::AutoResumptionHandler::Create(
      std::move(network_listener), std::move(task_manager), std::move(config));
}

// static
void DownloadManagerService::OnDownloadCanceled(
    download::DownloadItem* download,
    DownloadController::DownloadCancelReason reason) {
  if (download->IsTransient()) {
    LOG(WARNING) << "Transient download should not have user interaction!";
    return;
  }

  // Inform the user in Java UI about file writing failures.
  bool has_no_external_storage =
      (reason == DownloadController::CANCEL_REASON_NO_EXTERNAL_STORAGE);
  JNIEnv* env = base::android::AttachCurrentThread();

  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, download);
  Java_DownloadManagerService_onDownloadItemCanceled(env, j_item,
                                                     has_no_external_storage);
  DownloadController::RecordDownloadCancelReason(reason);
}

// static
DownloadManagerService* DownloadManagerService::GetInstance() {
  return base::Singleton<DownloadManagerService>::get();
}

// static
ScopedJavaLocalRef<jobject> DownloadManagerService::CreateJavaDownloadInfo(
    JNIEnv* env,
    download::DownloadItem* item) {
  bool user_initiated =
      (item->GetTransitionType() & ui::PAGE_TRANSITION_FROM_ADDRESS_BAR) ||
      PageTransitionCoreTypeIs(item->GetTransitionType(),
                               ui::PAGE_TRANSITION_TYPED) ||
      PageTransitionCoreTypeIs(item->GetTransitionType(),
                               ui::PAGE_TRANSITION_AUTO_BOOKMARK) ||
      PageTransitionCoreTypeIs(item->GetTransitionType(),
                               ui::PAGE_TRANSITION_GENERATED) ||
      PageTransitionCoreTypeIs(item->GetTransitionType(),
                               ui::PAGE_TRANSITION_RELOAD) ||
      PageTransitionCoreTypeIs(item->GetTransitionType(),
                               ui::PAGE_TRANSITION_KEYWORD);
  bool has_user_gesture = item->HasUserGesture() || user_initiated;

  base::TimeDelta time_delta;
  bool time_remaining_known = item->TimeRemaining(&time_delta);
  std::string original_url = item->GetOriginalUrl().SchemeIs(url::kDataScheme)
                                 ? std::string()
                                 : item->GetOriginalUrl().spec();
  content::BrowserContext* browser_context =
      content::DownloadItemUtils::GetBrowserContext(item);
  return Java_DownloadInfo_createDownloadInfo(
      env, ConvertUTF8ToJavaString(env, item->GetGuid()),
      ConvertUTF8ToJavaString(env, item->GetFileNameToReportUser().value()),
      ConvertUTF8ToJavaString(env, item->GetTargetFilePath().value()),
      ConvertUTF8ToJavaString(env, item->GetTabUrl().spec()),
      ConvertUTF8ToJavaString(env, item->GetMimeType()),
      item->GetReceivedBytes(), item->GetTotalBytes(),
      browser_context ? browser_context->IsOffTheRecord() : false,
      item->GetState(), item->PercentComplete(), item->IsPaused(),
      has_user_gesture, item->CanResume(), item->IsParallelDownload(),
      ConvertUTF8ToJavaString(env, original_url),
      ConvertUTF8ToJavaString(env, item->GetReferrerUrl().spec()),
      time_remaining_known ? time_delta.InMilliseconds()
                           : kUnknownRemainingTime,
      item->GetLastAccessTime().ToJavaTime(), item->IsDangerous(),
      static_cast<int>(
          OfflineItemUtils::ConvertDownloadInterruptReasonToFailState(
              item->GetLastReason())));
}

static jlong JNI_DownloadManagerService_Init(JNIEnv* env,
                                             const JavaParamRef<jobject>& jobj,
                                             jboolean is_full_browser_started) {
  DownloadManagerService* service = DownloadManagerService::GetInstance();
  service->Init(env, jobj, is_full_browser_started);
  return reinterpret_cast<intptr_t>(service);
}

DownloadManagerService::DownloadActionParams::DownloadActionParams(
    DownloadAction download_action)
    : action(download_action), has_user_gesture(false) {}

DownloadManagerService::DownloadActionParams::DownloadActionParams(
    DownloadAction download_action,
    bool user_gesture)
    : action(download_action), has_user_gesture(user_gesture) {}

DownloadManagerService::DownloadActionParams::DownloadActionParams(
    const DownloadActionParams& other)
    : action(other.action), has_user_gesture(other.has_user_gesture) {}

DownloadManagerService::DownloadManagerService()
    : is_manager_initialized_(false),
      is_pending_downloads_loaded_(false),
      pending_get_downloads_actions_(NONE) {}

DownloadManagerService::~DownloadManagerService() {}

void DownloadManagerService::BindServiceRequest(
    service_manager::mojom::ServiceRequest request) {
  service_binding_.Bind(std::move(request));
}

void DownloadManagerService::Init(JNIEnv* env,
                                  jobject obj,
                                  bool is_full_browser_started) {
  java_ref_.Reset(env, obj);
  if (is_full_browser_started)
    OnFullBrowserStarted(env, obj);
  else
    CreateInProgressDownloadManager();
}

void DownloadManagerService::OnFullBrowserStarted(JNIEnv* env, jobject obj) {
  registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CREATED,
                 content::NotificationService::AllSources());
  Profile* profile = ProfileManager::GetActiveUserProfile();
  content::DownloadManager* download_manager =
      content::BrowserContext::GetDownloadManager(profile);
  if (!download_manager)
    return;

  // Waiting for DownloadManager to initialize and carry out all the pending
  // actions
  if (download_manager->IsManagerInitialized())
    OnManagerInitialized();
  else
    download_manager->AddObserver(this);
}

void DownloadManagerService::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_PROFILE_CREATED: {
      Profile* profile = content::Source<Profile>(source).ptr();
      content::DownloadManager* manager =
          content::BrowserContext::GetDownloadManager(profile);
      if (!manager)
        break;

      auto& notifier = profile->IsOffTheRecord() ? off_the_record_notifier_
                                                 : original_notifier_;

      // Update notifiers to monitor any newly created DownloadManagers.
      if (!notifier || notifier->GetManager() != manager) {
        notifier =
            std::make_unique<download::AllDownloadItemNotifier>(manager, this);
      }
    } break;
    default:
      NOTREACHED();
  }
}

download::InProgressDownloadManager*
DownloadManagerService::RetriveInProgressDownloadManager(
    content::BrowserContext* context) {
  if (in_progress_manager_) {
    DCHECK(!context->IsOffTheRecord());
    // Set |is_pending_downloads_loaded_| to false so that we need to wait for
    // download history to initialize before performing new download actions.
    is_pending_downloads_loaded_ = false;
    return in_progress_manager_.release();
  }
  return nullptr;
}

void DownloadManagerService::ShowDownloadManager(bool show_prefetched_content) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_DownloadManagerService_showDownloadManager(
      env, java_ref_, static_cast<jboolean>(show_prefetched_content));
}

void DownloadManagerService::OpenDownload(download::DownloadItem* download,
                                          int source) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, download);

  Java_DownloadManagerService_openDownloadItem(env, java_ref_, j_item, source);
}

void DownloadManagerService::OpenDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record,
    jint source) {
  if (!is_manager_initialized_)
    return;

  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  download::DownloadItem* item = GetDownload(download_guid, is_off_the_record);
  if (!item)
    return;

  OpenDownload(item, source);
}

void DownloadManagerService::ResumeDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record,
    bool has_user_gesture) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  if (is_pending_downloads_loaded_ || is_off_the_record) {
    ResumeDownloadInternal(download_guid, is_off_the_record, has_user_gesture);
  } else {
    EnqueueDownloadAction(download_guid,
                          DownloadActionParams(RESUME, has_user_gesture));
  }
}

void DownloadManagerService::RetryDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record,
    bool has_user_gesture) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  if (is_pending_downloads_loaded_ || is_off_the_record)
    RetryDownloadInternal(download_guid, is_off_the_record, has_user_gesture);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(RETRY));
}

void DownloadManagerService::PauseDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  if (is_pending_downloads_loaded_ || is_off_the_record)
    PauseDownloadInternal(download_guid, is_off_the_record);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(PAUSE));
}

void DownloadManagerService::RemoveDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  if (is_manager_initialized_ || is_off_the_record)
    RemoveDownloadInternal(download_guid, is_off_the_record);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(REMOVE));
}

void DownloadManagerService::GetAllDownloads(JNIEnv* env,
                                             const JavaParamRef<jobject>& obj,
                                             bool is_off_the_record) {
  if (is_manager_initialized_)
    GetAllDownloadsInternal(is_off_the_record);
  else if (is_off_the_record)
    pending_get_downloads_actions_ |= OFF_THE_RECORD;
  else
    pending_get_downloads_actions_ |= REGULAR;
}

void DownloadManagerService::GetAllDownloadsInternal(bool is_off_the_record) {
  content::DownloadManager* manager = GetDownloadManager(is_off_the_record);
  if (java_ref_.is_null() || !manager)
    return;

  content::DownloadManager::DownloadVector all_items;
  manager->GetAllDownloads(&all_items);

  // Create a Java array of all of the visible DownloadItems.
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_download_item_list =
      Java_DownloadManagerService_createDownloadItemList(env, java_ref_);

  for (size_t i = 0; i < all_items.size(); i++) {
    download::DownloadItem* item = all_items[i];
    if (!ShouldShowDownloadItem(item))
      continue;

    ScopedJavaLocalRef<jobject> j_item =
        JNI_DownloadManagerService_CreateJavaDownloadItem(env, item);
    Java_DownloadManagerService_addDownloadItemToList(
        env, java_ref_, j_download_item_list, j_item);
  }

  Java_DownloadManagerService_onAllDownloadsRetrieved(
      env, java_ref_, j_download_item_list, is_off_the_record);
}

void DownloadManagerService::CheckForExternallyRemovedDownloads(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    bool is_off_the_record) {
  // Once the DownloadManager is initlaized, DownloadHistory will check for the
  // removal of history files. If the history query is not yet complete, ignore
  // requests to check for externally removed downloads.
  if (!is_manager_initialized_)
    return;

  content::DownloadManager* manager = GetDownloadManager(is_off_the_record);
  if (!manager)
    return;
  manager->CheckForHistoryFilesRemoval();
}

void DownloadManagerService::UpdateLastAccessTime(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  content::DownloadManager* manager = GetDownloadManager(is_off_the_record);
  if (!manager)
    return;

  download::DownloadItem* item = manager->GetDownloadByGuid(download_guid);
  if (item)
    item->SetLastAccessTime(base::Time::Now());
}

void DownloadManagerService::CancelDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    bool is_off_the_record) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  DownloadController::RecordDownloadCancelReason(
      DownloadController::CANCEL_REASON_ACTION_BUTTON);
  if (is_pending_downloads_loaded_ || is_off_the_record)
    CancelDownloadInternal(download_guid, is_off_the_record);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(CANCEL));
}

void DownloadManagerService::OnManagerInitialized() {
  is_manager_initialized_ = true;
  OnPendingDownloadsLoaded();

  // Respond to any requests to get all downloads.
  if (pending_get_downloads_actions_ & REGULAR)
    GetAllDownloadsInternal(false);
  if (pending_get_downloads_actions_ & OFF_THE_RECORD)
    GetAllDownloadsInternal(true);
}

void DownloadManagerService::OnManagerInitialized(
    content::DownloadManager* manager) {}

void DownloadManagerService::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (item->IsTransient())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, item);
  Java_DownloadManagerService_onDownloadItemCreated(env, java_ref_, j_item);
}

void DownloadManagerService::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (java_ref_.is_null())
    return;

  if (item->IsTemporary() || item->IsTransient())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, item);
  Java_DownloadManagerService_onDownloadItemUpdated(env, java_ref_, j_item);
}

void DownloadManagerService::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (java_ref_.is_null() || item->IsTransient())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  Java_DownloadManagerService_onDownloadItemRemoved(
      env, java_ref_, ConvertUTF8ToJavaString(env, item->GetGuid()),
      content::DownloadItemUtils::GetBrowserContext(item)->IsOffTheRecord());
}

void DownloadManagerService::ResumeDownloadInternal(
    const std::string& download_guid,
    bool is_off_the_record,
    bool has_user_gesture) {
  download::DownloadItem* item = GetDownload(download_guid, is_off_the_record);
  if (!item) {
    OnResumptionFailed(download_guid);
    return;
  }
  if (!item->CanResume()) {
    OnResumptionFailed(download_guid);
    return;
  }
  DownloadControllerBase::Get()->AboutToResumeDownload(item);
  item->Resume(has_user_gesture);
  if (!resume_callback_for_testing_.is_null())
    resume_callback_for_testing_.Run(true);
}

void DownloadManagerService::RetryDownloadInternal(
    const std::string& download_guid,
    bool is_off_the_record,
    bool has_user_gesture) {
  content::DownloadManager* manager = GetDownloadManager(is_off_the_record);
  if (!manager)
    return;

  download::DownloadItem* item = manager->GetDownloadByGuid(download_guid);
  if (!item)
    return;

  // Try to resume first.
  if (item->CanResume()) {
    item->Resume(has_user_gesture);
    return;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("download_manager_service_retry", R"(
        semantics {
          sender: "DownloadManagerService"
          description:
            "Retry a download by creating new network request."
          trigger:
            "User retries a download."
          data: "None."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but it is activated "
            "by direct user action."
          chrome_policy {
            DownloadRestrictions {
              DownloadRestrictions: 3
            }
          }
        })");
  auto download_url_params = std::make_unique<download::DownloadUrlParameters>(
      item->GetURL(), traffic_annotation);

  // Retry allows redirect.
  download_url_params->set_follow_cross_origin_redirects(true);

  // Retry is triggered through user gesture, and don't have renderer
  // associated, content initiated has to be false to avoid download being
  // blocked.
  download_url_params->set_content_initiated(false);

  // TODO(xingliu): See if we need to persist the referrer policy. Never clear
  // referrer potentially may result in delivering unexpected referrer to web
  // servers.
  download_url_params->set_referrer_policy(
      net::URLRequest::NEVER_CLEAR_REFERRER);
  download_url_params->set_referrer(item->GetReferrerUrl());
  download_url_params->set_download_source(download::DownloadSource::RETRY);

  // Creates a new download.
  manager->DownloadUrl(std::move(download_url_params));

  // Removes the current download.
  item->Remove();
}

void DownloadManagerService::CancelDownloadInternal(
    const std::string& download_guid,
    bool is_off_the_record) {
  download::DownloadItem* item = GetDownload(download_guid, is_off_the_record);
  if (item) {
    // Remove the observer first to avoid item->Cancel() causing re-entrance
    // issue.
    item->RemoveObserver(DownloadControllerBase::Get());
    item->Cancel(true);
  }
}

void DownloadManagerService::PauseDownloadInternal(
    const std::string& download_guid,
    bool is_off_the_record) {
  download::DownloadItem* item = GetDownload(download_guid, is_off_the_record);
  if (item)
    item->Pause();
}

void DownloadManagerService::RemoveDownloadInternal(
    const std::string& download_guid,
    bool is_off_the_record) {
  download::DownloadItem* item = GetDownload(download_guid, is_off_the_record);
  if (item)
    item->Remove();
}

void DownloadManagerService::EnqueueDownloadAction(
    const std::string& download_guid,
    const DownloadActionParams& params) {
  auto iter = pending_actions_.find(download_guid);
  if (iter == pending_actions_.end()) {
    pending_actions_.insert(std::make_pair(download_guid, params));
    return;
  }
  switch (params.action) {
    case RESUME:
      if (iter->second.action == PAUSE)
        iter->second = params;
      break;
    case PAUSE:
      if (iter->second.action == RESUME)
        iter->second = params;
      break;
    case CANCEL:
      iter->second = params;
      break;
    case REMOVE:
      iter->second = params;
      break;
    default:
      NOTREACHED();
      break;
  }
}

void DownloadManagerService::OnResumptionFailed(
    const std::string& download_guid) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&DownloadManagerService::OnResumptionFailedInternal,
                     base::Unretained(this), download_guid));
  DownloadController::RecordDownloadCancelReason(
      DownloadController::CANCEL_REASON_NOT_CANCELED);
}

void DownloadManagerService::OnResumptionFailedInternal(
    const std::string& download_guid) {
  if (!java_ref_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_DownloadManagerService_onResumptionFailed(
        env, java_ref_, ConvertUTF8ToJavaString(env, download_guid));
  }
  if (!resume_callback_for_testing_.is_null())
    resume_callback_for_testing_.Run(false);
}

void DownloadManagerService::GetAllDownloads(
    content::DownloadManager::DownloadVector* all_items,
    bool is_off_the_record) {
  if (in_progress_manager_) {
    in_progress_manager_->GetAllDownloads(all_items);
  } else {
    content::DownloadManager* manager = GetDownloadManager(is_off_the_record);
    if (manager)
      manager->GetAllDownloads(all_items);
  }
}

download::DownloadItem* DownloadManagerService::GetDownload(
    const std::string& download_guid,
    bool is_off_the_record) {
  if (in_progress_manager_) {
    DCHECK(!is_off_the_record);
    return in_progress_manager_->GetDownloadByGuid(download_guid);
  }

  content::DownloadManager* manager = GetDownloadManager(is_off_the_record);
  if (manager)
    return manager->GetDownloadByGuid(download_guid);
  return nullptr;
}

void DownloadManagerService::CreateInProgressDownloadManager() {
  DCHECK(!in_progress_manager_);
  base::FilePath data_dir;
  base::android::GetDataDirectory(&data_dir);
  in_progress_manager_ = std::make_unique<download::InProgressDownloadManager>(
      nullptr, data_dir.Append(chrome::kInitialProfile),
      base::BindRepeating(&IgnoreOriginSecurityCheck),
      base::BindRepeating(&content::DownloadRequestUtils::IsURLSafe));
  content::GetNetworkServiceFromConnector(service_binding_.GetConnector());
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      SystemNetworkContextManager::GetInstance()->GetSharedURLLoaderFactory();
  in_progress_manager_->set_url_loader_factory_getter(
      base::MakeRefCounted<download::DownloadURLLoaderFactoryGetterImpl>(
          factory->Clone()));
  in_progress_manager_->NotifyWhenInitialized(
      base::BindOnce(&DownloadManagerService::OnPendingDownloadsLoaded,
                     base::Unretained(this)));
  in_progress_manager_->set_download_start_observer(
      DownloadControllerBase::Get());
}

void DownloadManagerService::OnPendingDownloadsLoaded() {
  // If |in_progress_manager_| is null, wait for DownloadManager to initialize
  // before performing any pending actions.
  if (!in_progress_manager_ && !is_manager_initialized_)
    return;
  is_pending_downloads_loaded_ = true;

  // Kick-off the auto-resumption handler.
  content::DownloadManager::DownloadVector all_items;
  if (in_progress_manager_) {
    in_progress_manager_->GetAllDownloads(&all_items);
  } else {
    content::DownloadManager* manager = GetDownloadManager(false);
    if (manager)
      manager->GetAllDownloads(&all_items);
  }

  if (!download::AutoResumptionHandler::Get())
    CreateAutoResumptionHandler();

  download::AutoResumptionHandler::Get()->SetResumableDownloads(all_items);

  for (auto iter = pending_actions_.begin(); iter != pending_actions_.end();
       ++iter) {
    DownloadActionParams params = iter->second;
    std::string download_guid = iter->first;
    switch (params.action) {
      case RESUME:
        ResumeDownloadInternal(download_guid, false, params.has_user_gesture);
        break;
      case PAUSE:
        PauseDownloadInternal(download_guid, false);
        break;
      case CANCEL:
        CancelDownloadInternal(download_guid, false);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  pending_actions_.clear();
}

content::DownloadManager* DownloadManagerService::GetDownloadManager(
    bool is_off_the_record) {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  if (is_off_the_record)
    profile = profile->GetOffTheRecordProfile();

  auto& notifier =
      is_off_the_record ? off_the_record_notifier_ : original_notifier_;
  content::DownloadManager* manager =
      content::BrowserContext::GetDownloadManager(profile);
  if (!manager) {
    notifier.reset();
    return nullptr;
  }

  // Update notifiers to monitor any newly created DownloadManagers.
  if (!notifier || notifier->GetManager() != manager) {
    notifier =
        std::make_unique<download::AllDownloadItemNotifier>(manager, this);
  }
  return manager;
}

void DownloadManagerService::RenameDownload(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& id,
    const JavaParamRef<jstring>& name,
    const JavaParamRef<jobject>& j_callback,
    bool is_off_the_record) {
  std::string download_guid = ConvertJavaStringToUTF8(id);
  download::DownloadItem* item = GetDownload(download_guid, is_off_the_record);
  if (!item) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &RenameItemCallback,
            base::android::ScopedJavaGlobalRef<jobject>(env, j_callback),
            download::DownloadItem::DownloadRenameResult::FAILURE_UNAVAILABLE));

    return;
  }
  std::string target_name = ConvertJavaStringToUTF8(name);
  base::OnceCallback<void(download::DownloadItem::DownloadRenameResult)>
      callback = base::BindOnce(
          &RenameItemCallback,
          base::android::ScopedJavaGlobalRef<jobject>(env, j_callback));
  item->Rename(base::FilePath(target_name), std::move(callback));
}

void DownloadManagerService::CreateInterruptedDownloadForTest(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jurl,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jstring>& jtarget_path) {
  if (!in_progress_manager_)
    return;
  std::vector<GURL> url_chain;
  url_chain.emplace_back(ConvertJavaStringToUTF8(env, jurl));
  base::FilePath target_path(ConvertJavaStringToUTF8(env, jtarget_path));
  in_progress_manager_->AddInProgressDownloadForTest(
      std::make_unique<download::DownloadItemImpl>(
          in_progress_manager_.get(),
          ConvertJavaStringToUTF8(env, jdownload_guid), 1,
          target_path.AddExtension("crdownload"), target_path, url_chain,
          GURL(), GURL(), GURL(), GURL(), "", "", base::Time(), base::Time(),
          "", "", 0, -1, 0, "", download::DownloadItem::INTERRUPTED,
          download::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
          download::DOWNLOAD_INTERRUPT_REASON_CRASH, false, false, false,
          base::Time(), false,
          std::vector<download::DownloadItem::ReceivedSlice>()));
}

// static
jboolean JNI_DownloadManagerService_IsSupportedMimeType(
    JNIEnv* env,
    const JavaParamRef<jstring>& jmime_type) {
  std::string mime_type = ConvertJavaStringToUTF8(env, jmime_type);
  return blink::IsSupportedMimeType(mime_type);
}

// static
jint JNI_DownloadManagerService_GetAutoResumptionLimit(JNIEnv* env) {
  std::string value = base::GetFieldTrialParamValueByFeature(
      chrome::android::kDownloadAutoResumptionThrottling,
      kAutoResumptionLimitParamName);
  int auto_resumption_limit;
  return base::StringToInt(value, &auto_resumption_limit)
             ? auto_resumption_limit
             : kDefaultAutoResumptionLimit;
}
