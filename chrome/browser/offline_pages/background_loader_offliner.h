// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_OFFLINE_PAGES_BACKGROUND_LOADER_OFFLINER_H_
#define CHROME_BROWSER_OFFLINE_PAGES_BACKGROUND_LOADER_OFFLINER_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/offline_pages/resource_loading_observer.h"
#include "components/offline_pages/content/background_loader/background_loader_contents.h"
#include "components/offline_pages/core/background/load_termination_listener.h"
#include "components/offline_pages/core/background/offliner.h"
#include "components/offline_pages/core/background_snapshot_controller.h"
#include "components/offline_pages/core/offline_page_types.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace offline_pages {

class OfflinerPolicy;
class OfflinePageModel;

class PageRenovationLoader;
class PageRenovator;

struct RequestStats {
  int requested;
  int completed;

  RequestStats() : requested(0), completed(0) {}
};

// An Offliner implementation that attempts client-side rendering and saving
// of an offline page. It uses the BackgroundLoader to load the page and the
// OfflinePageModel to save it. Only one request may be active at a time.
class BackgroundLoaderOffliner
    : public Offliner,
      public background_loader::BackgroundLoaderContents::Delegate,
      public content::WebContentsObserver,
      public BackgroundSnapshotController::Client,
      public ResourceLoadingObserver {
 public:
  BackgroundLoaderOffliner(
      content::BrowserContext* browser_context,
      const OfflinerPolicy* policy,
      OfflinePageModel* offline_page_model,
      std::unique_ptr<LoadTerminationListener> load_termination_listener);
  ~BackgroundLoaderOffliner() override;

  static BackgroundLoaderOffliner* FromWebContents(
      content::WebContents* contents);

  // Offliner implementation.
  bool LoadAndSave(const SavePageRequest& request,
                   CompletionCallback completion_callback,
                   const ProgressCallback& progress_callback) override;
  bool Cancel(CancelCallback callback) override;
  void TerminateLoadIfInProgress() override;
  bool HandleTimeout(int64_t request_id) override;

  // BackgroundLoaderContents::Delegate implementation.
  // Called when a navigation resulted in a single-file download. e.g.
  // When user navigated to a pdf page while offline and clicks on the
  // "Download page later" button.
  void CanDownload(base::OnceCallback<void(bool)> callback) override;

  // WebContentsObserver implementation.
  void DocumentAvailableInMainFrame() override;
  void DocumentOnLoadCompletedInMainFrame() override;
  void RenderProcessGone(base::TerminationStatus status) override;
  void WebContentsDestroyed() override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // BackgroundSnapshotController::Client implementation.
  void StartSnapshot() override;
  void RunRenovations() override;

  void SetBackgroundSnapshotControllerForTest(
      std::unique_ptr<BackgroundSnapshotController> controller);

  // ResourceLoadingObserver implemenation
  void ObserveResourceLoading(ResourceLoadingObserver::ResourceDataType type,
                              bool started) override;
  void OnNetworkBytesChanged(int64_t bytes) override;

 protected:
  // Called to reset the loader.
  virtual void ResetLoader();

 private:
  friend class TestBackgroundLoaderOffliner;
  friend class BackgroundLoaderOfflinerTest;

  enum SaveState { NONE, SAVING, DELETE_AFTER_SAVE };
  enum PageLoadState {
    SUCCESS,
    RETRIABLE_NET_ERROR,
    RETRIABLE_HTTP_ERROR,
    NONRETRIABLE
  };

  // Called when the page has been saved.
  void OnPageSaved(SavePageResult save_result, int64_t offline_id);

  // Called to reset internal loader and observer state.
  void ResetState();

  // Called to attach 'this' as the observer to the loader.
  void AttachObservers();

  // Called to remember at what time we started loading.
  void MarkLoadStartTime();

  // Called to add a loading signal as we observe it.
  void AddLoadingSignal(const char* signal_name);

  // Called by PageRenovator callback when renovations complete.
  void RenovationsCompleted();

  void DeleteOfflinePageCallback(const SavePageRequest& request,
                                 DeletePageResult result);

  // Testing method to examine resource stats.
  RequestStats* GetRequestStatsForTest() { return stats_; }

  std::unique_ptr<background_loader::BackgroundLoaderContents> loader_;
  // Not owned.
  content::BrowserContext* browser_context_;
  // Not owned.
  OfflinePageModel* offline_page_model_;
  // Not owned.
  const OfflinerPolicy* policy_;
  // Tracks pending request, if any.
  std::unique_ptr<SavePageRequest> pending_request_;
  // Handles determining when a page should be snapshotted.
  std::unique_ptr<BackgroundSnapshotController> snapshot_controller_;
  // Callback when pending request completes.
  CompletionCallback completion_callback_;
  // Callback to report progress.
  ProgressCallback progress_callback_;
  // LoadTerminationListener to monitor external conditions requiring immediate
  // termination of the offlining operation (memory conditions etc).
  std::unique_ptr<LoadTerminationListener> load_termination_listener_;
  // Whether we are on a low-end device.
  bool is_low_end_device_;

  // PageRenovationLoader must live longer than the PageRenovator.
  std::unique_ptr<PageRenovationLoader> page_renovation_loader_;
  // Per-offliner PageRenovator instance.
  std::unique_ptr<PageRenovator> page_renovator_;

  // Save state.
  SaveState save_state_;
  // Page load state.
  PageLoadState page_load_state_;
  // Network bytes loaded.
  int64_t network_bytes_;
  // Whether the low bar of snapshot quality has been met.
  bool is_low_bar_met_;
  // Whether the snapshot is on the last retry.
  bool did_snapshot_on_last_retry_;

  // Time in ticks of when we start loading the page.
  base::TimeTicks load_start_time_;

  // Saves loading signals.
  // TODO(petewil): We will be replacing this with the new snapshot controller.
  base::DictionaryValue signal_data_;

  // Callback for cancel.
  CancelCallback cancel_callback_;

  // Holds stats for resource request status for resource types we track.
  RequestStats stats_[ResourceDataType::RESOURCE_DATA_TYPE_COUNT];

  base::WeakPtrFactory<BackgroundLoaderOffliner> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(BackgroundLoaderOffliner);
};

}  // namespace offline_pages
#endif  // CHROME_BROWSER_OFFLINE_PAGES_BACKGROUND_LOADER_OFFLINER_H_
