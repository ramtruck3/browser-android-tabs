// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/loading_predictor_page_load_metrics_observer.h"

#include <memory>

#include "chrome/browser/page_load_metrics/observers/page_load_metrics_observer_test_harness.h"
#include "chrome/browser/page_load_metrics/page_load_tracker.h"
#include "chrome/browser/predictors/loading_data_collector.h"
#include "chrome/browser/predictors/resource_prefetch_common.h"
#include "chrome/browser/predictors/resource_prefetch_predictor.h"
#include "chrome/common/page_load_metrics/test/page_load_metrics_test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using predictors::LoadingDataCollector;
using predictors::ResourcePrefetchPredictor;

class MockResourcePrefetchPredictor : public ResourcePrefetchPredictor {
 public:
  MockResourcePrefetchPredictor(
      const predictors::LoadingPredictorConfig& config,
      Profile* profile)
      : ResourcePrefetchPredictor(config, profile) {}

  MOCK_CONST_METHOD1(IsUrlPrefetchable, bool(const GURL& main_frame_url));
  MOCK_CONST_METHOD1(IsUrlPreconnectable, bool(const GURL& main_frame_url));

  ~MockResourcePrefetchPredictor() override {}
};

class LoadingPredictorPageLoadMetricsObserverTest
    : public page_load_metrics::PageLoadMetricsObserverTestHarness {
 protected:
  void SetUp() override {
    page_load_metrics::PageLoadMetricsObserverTestHarness::SetUp();
    predictors::LoadingPredictorConfig config;
    predictor_ =
        std::make_unique<testing::StrictMock<MockResourcePrefetchPredictor>>(
            config, profile());
    // The base class of MockResourcePrefetchPredictor constructs the
    // PredictorDatabase for the profile. The PredictorDatabase is initialized
    // asynchronously and we have to wait for the initialization completion.
    content::RunAllTasksUntilIdle();
    page_load_metrics::InitPageLoadTimingForTest(&timing_);
    collector_ = std::make_unique<LoadingDataCollector>(predictor_.get(),
                                                        nullptr, config);
    timing_.navigation_start = base::Time::FromDoubleT(1);
    timing_.paint_timing->first_paint = base::TimeDelta::FromSeconds(2);
    timing_.paint_timing->first_contentful_paint =
        base::TimeDelta::FromSeconds(3);
    timing_.paint_timing->first_meaningful_paint =
        base::TimeDelta::FromSeconds(4);
    PopulateRequiredTimingFields(&timing_);
  }

  void RegisterObservers(page_load_metrics::PageLoadTracker* tracker) override {
    tracker->AddObserver(
        std::make_unique<LoadingPredictorPageLoadMetricsObserver>(
            predictor_.get(), collector_.get()));
  }

  void TestHistogramsRecorded(bool is_preconnectable) {
    const GURL main_frame_url("https://www.google.com");
    EXPECT_CALL(*predictor_.get(), IsUrlPreconnectable(main_frame_url))
        .WillOnce(testing::Return(is_preconnectable));

    NavigateAndCommit(main_frame_url);
    SimulateTimingUpdate(timing_);

    histogram_tester().ExpectTotalCount(
        internal::kHistogramLoadingPredictorFirstContentfulPaintPreconnectable,
        is_preconnectable ? 1 : 0);
    histogram_tester().ExpectTotalCount(
        internal::kHistogramLoadingPredictorFirstMeaningfulPaintPreconnectable,
        is_preconnectable ? 1 : 0);
  }

  std::unique_ptr<testing::StrictMock<MockResourcePrefetchPredictor>>
      predictor_;
  page_load_metrics::mojom::PageLoadTiming timing_;
  std::unique_ptr<LoadingDataCollector> collector_;
};

TEST_F(LoadingPredictorPageLoadMetricsObserverTest, PreconnectableIsRecorded) {
  TestHistogramsRecorded(true);
}

TEST_F(LoadingPredictorPageLoadMetricsObserverTest,
       NotPreconnectableIsNotRecorded) {
  TestHistogramsRecorded(false);
}
