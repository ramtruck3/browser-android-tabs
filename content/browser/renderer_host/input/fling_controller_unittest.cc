// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/input/fling_controller.h"

#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/simple_test_tick_clock.h"
#include "build/build_config.h"
#include "content/browser/renderer_host/input/gesture_event_queue.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/blink/fling_booster.h"

using blink::WebGestureEvent;
using blink::WebInputEvent;
using blink::WebMouseWheelEvent;

namespace {
constexpr double kFrameDelta = 1000.0 / 60.0;
}  // namespace

namespace content {

class FakeFlingController : public FlingController {
 public:
  FakeFlingController(FlingControllerEventSenderClient* event_sender_client,
                      FlingControllerSchedulerClient* scheduler_client,
                      const Config& config)
      : FlingController(event_sender_client, scheduler_client, config) {}

  bool FlingBoosted() const { return fling_booster_->fling_boosted(); }
};

class FlingControllerTest : public FlingControllerEventSenderClient,
                            public FlingControllerSchedulerClient,
                            public testing::TestWithParam<bool> {
 public:
  // testing::Test
  FlingControllerTest()
      : needs_begin_frame_for_fling_progress_(GetParam()),
        scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::UI) {}

  ~FlingControllerTest() override {}

  void SetUp() override {
    fling_controller_ = std::make_unique<FakeFlingController>(
        this, this, FlingController::Config());
    fling_controller_->set_clock_for_testing(&mock_clock_);
    AdvanceTime();
  }

  // FlingControllerEventSenderClient
  void SendGeneratedWheelEvent(
      const MouseWheelEventWithLatencyInfo& wheel_event) override {
    wheel_event_count_++;
    last_sent_wheel_ = wheel_event.event;
    first_wheel_event_sent_ = true;

    if (wheel_event.event.momentum_phase == WebMouseWheelEvent::kPhaseEnded)
      first_wheel_event_sent_ = false;
  }
  void SendGeneratedGestureScrollEvents(
      const GestureEventWithLatencyInfo& gesture_event) override {
    sent_scroll_gesture_count_++;
    last_sent_gesture_ = gesture_event.event;
  }

  // FlingControllerSchedulerClient
  void ScheduleFlingProgress(
      base::WeakPtr<FlingController> fling_controller) override {
    DCHECK(!scheduled_next_fling_progress_);
    scheduled_next_fling_progress_ = true;
  }
  void DidStopFlingingOnBrowser(
      base::WeakPtr<FlingController> fling_controller) override {
    notified_client_after_fling_stop_ = true;
  }
  bool NeedsBeginFrameForFlingProgress() override {
    return needs_begin_frame_for_fling_progress_;
  }

  void SimulateFlingStart(blink::WebGestureDevice source_device,
                          const gfx::Vector2dF& velocity,
                          bool wait_before_processing = true) {
    scheduled_next_fling_progress_ = false;
    sent_scroll_gesture_count_ = 0;
    WebGestureEvent fling_start(WebInputEvent::kGestureFlingStart, 0,
                                NowTicks(), source_device);
    fling_start.data.fling_start.velocity_x = velocity.x();
    fling_start.data.fling_start.velocity_y = velocity.y();
    GestureEventWithLatencyInfo fling_start_with_latency(fling_start);
    if (wait_before_processing) {
      // Wait for up to one frame before processing the event.
      AdvanceTime(base::RandInt(0, static_cast<int>(kFrameDelta)));
    }
    fling_controller_->ObserveAndMaybeConsumeGestureEvent(
        fling_start_with_latency);
  }

  void SimulateScrollUpdate(blink::WebGestureDevice source_device,
                            const gfx::Vector2dF& delta) {
    WebGestureEvent scroll_update(WebInputEvent::kGestureScrollUpdate, 0,
                                  NowTicks(), source_device);
    scroll_update.data.scroll_update.delta_x = delta.x();
    scroll_update.data.scroll_update.delta_y = delta.y();
    scroll_update.data.scroll_update.velocity_x = delta.x();
    scroll_update.data.scroll_update.velocity_y = delta.y();
    scroll_update.data.scroll_update.inertial_phase =
        WebGestureEvent::kNonMomentumPhase;
    scroll_update.data.scroll_update.delta_units =
        WebGestureEvent::kPrecisePixels;
    GestureEventWithLatencyInfo scroll_update_with_latency(scroll_update);

    fling_controller_->ObserveAndMaybeConsumeGestureEvent(
        scroll_update_with_latency);
  }

  void SimulateFlingCancel(blink::WebGestureDevice source_device) {
    notified_client_after_fling_stop_ = false;
    WebGestureEvent fling_cancel(WebInputEvent::kGestureFlingCancel, 0,
                                 NowTicks(), source_device);
    // autoscroll fling cancel doesn't allow fling boosting.
    if (source_device == blink::WebGestureDevice::kSyntheticAutoscroll)
      fling_cancel.data.fling_cancel.prevent_boosting = true;
    GestureEventWithLatencyInfo fling_cancel_with_latency(fling_cancel);
    fling_controller_->ObserveAndMaybeConsumeGestureEvent(
        fling_cancel_with_latency);
  }

  void ProgressFling(base::TimeTicks current_time) {
    DCHECK(scheduled_next_fling_progress_);
    scheduled_next_fling_progress_ = false;
    fling_controller_->ProgressFling(current_time);
  }

  bool FlingInProgress() { return fling_controller_->fling_in_progress(); }
  bool FlingBoosted() { return fling_controller_->FlingBoosted(); }

  void AdvanceTime(double time_delta_ms = kFrameDelta) {
    mock_clock_.Advance(base::TimeDelta::FromMillisecondsD(time_delta_ms));
  }

  base::TimeTicks NowTicks() const { return mock_clock_.NowTicks(); }

 protected:
  std::unique_ptr<FakeFlingController> fling_controller_;
  int wheel_event_count_ = 0;
  WebMouseWheelEvent last_sent_wheel_;
  WebGestureEvent last_sent_gesture_;
  bool scheduled_next_fling_progress_ = false;
  bool notified_client_after_fling_stop_ = false;
  bool first_wheel_event_sent_ = false;
  int sent_scroll_gesture_count_ = 0;

 private:
  base::SimpleTestTickClock mock_clock_;

  bool needs_begin_frame_for_fling_progress_;
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  DISALLOW_COPY_AND_ASSIGN(FlingControllerTest);
};

INSTANTIATE_TEST_SUITE_P(, FlingControllerTest, testing::Bool());

TEST_P(FlingControllerTest,
       ControllerSendsWheelEndOnTouchpadFlingWithZeroVelocity) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchpad, gfx::Vector2dF());
  // The controller doesn't start a fling and sends a wheel end event
  // immediately.
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebMouseWheelEvent::kPhaseEnded, last_sent_wheel_.momentum_phase);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_x);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_y);
}

TEST_P(FlingControllerTest,
       ControllerSendsGSEOnTouchscreenFlingWithZeroVelocity) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen, gfx::Vector2dF());
  // The controller doesn't start a fling and sends a GSE immediately.
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebInputEvent::kGestureScrollEnd, last_sent_gesture_.GetType());
}

TEST_P(FlingControllerTest, ControllerHandlesTouchpadGestureFling) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchpad,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  // Processing GFS will send the first fling progress event if the time delta
  // between the timestamp of the GFS and the time that ProcessGestureFlingStart
  // is called is large enough.
  bool process_GFS_sent_first_event = first_wheel_event_sent_;

  AdvanceTime();
  ProgressFling(NowTicks());

  if (!process_GFS_sent_first_event) {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseBegan, last_sent_wheel_.momentum_phase);
  } else {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged,
              last_sent_wheel_.momentum_phase);
  }
  EXPECT_GT(last_sent_wheel_.delta_x, 0.f);

  // The rest of the wheel events must have momentum_phase == KPhaseChanged.
  AdvanceTime();
  ProgressFling(NowTicks());
  EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged, last_sent_wheel_.momentum_phase);
  EXPECT_GT(last_sent_wheel_.delta_x, 0.f);

  // Now cancel the fling. The GFC will get suppressed by fling booster.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchpad);
  EXPECT_TRUE(FlingInProgress());

  // Wait for the boosting timer to expire. The delayed cancelation must work.
  AdvanceTime(500);
  ProgressFling(NowTicks());
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebMouseWheelEvent::kPhaseEnded, last_sent_wheel_.momentum_phase);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_x);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_y);
}

// Ensure that the start time of a fling is measured from the last received
// GSU. This ensures that the first progress fling during FlingStart should
// send significant delta. If we're using the FlingStart as the start time, we
// would send none or very little delta.
TEST_P(FlingControllerTest, FlingStartsAtLastScrollUpdate) {
  SimulateScrollUpdate(blink::WebGestureDevice::kTouchscreen,
                       gfx::Vector2dF(1000, 0));
  double time_to_advance_ms = 30.0;
  AdvanceTime(time_to_advance_ms);
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0), /*wait_before_processing=*/false);
  EXPECT_TRUE(FlingInProgress());

  if (NeedsBeginFrameForFlingProgress())
    ProgressFling(NowTicks());

  // We haven't advanced time since the FlingStart. Ensure we still send a
  // significant amount of delta (~0.030sec * 1000pixels/sec) since we should
  // be measuring the time since the last GSU.
  EXPECT_EQ(1, sent_scroll_gesture_count_);
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_NEAR(last_sent_gesture_.data.scroll_update.delta_x, 30.0, 5);
}

TEST_P(FlingControllerTest, ControllerHandlesTouchscreenGestureFling) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());

  // The fling progress will generate and send GSU events with inertial state.
  AdvanceTime();
  ProgressFling(NowTicks());
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_EQ(WebGestureEvent::kMomentumPhase,
            last_sent_gesture_.data.scroll_update.inertial_phase);
  EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);

  // Now cancel the fling. The GFC will get suppressed by fling booster.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchscreen);
  EXPECT_TRUE(FlingInProgress());

  // Wait for the boosting timer to expire. The delayed cancelation must work.
  AdvanceTime(500);
  ProgressFling(NowTicks());
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebInputEvent::kGestureScrollEnd, last_sent_gesture_.GetType());
}

TEST_P(FlingControllerTest, ControllerSendsWheelEndWhenTouchpadFlingIsOver) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchpad,
                     gfx::Vector2dF(100, 0));
  EXPECT_TRUE(FlingInProgress());
  // Processing GFS will send the first fling progress event if the time delta
  // between the timestamp of the GFS and the time that ProcessGestureFlingStart
  // is called is large enough.
  bool process_GFS_sent_first_event = first_wheel_event_sent_;

  AdvanceTime();
  ProgressFling(NowTicks());
  if (!process_GFS_sent_first_event) {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseBegan, last_sent_wheel_.momentum_phase);
  } else {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged,
              last_sent_wheel_.momentum_phase);
  }
  EXPECT_GT(last_sent_wheel_.delta_x, 0.f);

  AdvanceTime();
  ProgressFling(NowTicks());
  while (FlingInProgress()) {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged,
              last_sent_wheel_.momentum_phase);
    EXPECT_GT(last_sent_wheel_.delta_x, 0.f);
    AdvanceTime();
    ProgressFling(NowTicks());
  }

  EXPECT_EQ(WebMouseWheelEvent::kPhaseEnded, last_sent_wheel_.momentum_phase);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_x);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_y);
}

TEST_P(FlingControllerTest, ControllerSendsGSEWhenTouchscreenFlingIsOver) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(100, 0));
  EXPECT_TRUE(FlingInProgress());

  AdvanceTime();
  ProgressFling(NowTicks());
  while (FlingInProgress()) {
    ASSERT_EQ(WebInputEvent::kGestureScrollUpdate,
              last_sent_gesture_.GetType());
    EXPECT_EQ(WebGestureEvent::kMomentumPhase,
              last_sent_gesture_.data.scroll_update.inertial_phase);
    EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);
    AdvanceTime();
    ProgressFling(NowTicks());
  }

  EXPECT_EQ(WebInputEvent::kGestureScrollEnd, last_sent_gesture_.GetType());
}

TEST_P(FlingControllerTest, EarlyTouchpadFlingCancelationOnFlingStop) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchpad,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  // Processing GFS will send the first fling progress event if the time delta
  // between the timestamp of the GFS and the time that ProcessGestureFlingStart
  // is called is large enough.
  bool process_GFS_sent_first_event = first_wheel_event_sent_;

  AdvanceTime();
  ProgressFling(NowTicks());
  if (!process_GFS_sent_first_event) {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseBegan, last_sent_wheel_.momentum_phase);
  } else {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged,
              last_sent_wheel_.momentum_phase);
  }
  EXPECT_GT(last_sent_wheel_.delta_x, 0.f);

  fling_controller_->StopFling();
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebMouseWheelEvent::kPhaseEnded, last_sent_wheel_.momentum_phase);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_x);
  EXPECT_EQ(0.f, last_sent_wheel_.delta_y);
}

TEST_P(FlingControllerTest, EarlyTouchscreenFlingCancelationOnFlingStop) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());

  // progress fling must send GSU events.
  AdvanceTime();
  ProgressFling(NowTicks());
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_EQ(WebGestureEvent::kMomentumPhase,
            last_sent_gesture_.data.scroll_update.inertial_phase);
  EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);

  fling_controller_->StopFling();
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebInputEvent::kGestureScrollEnd, last_sent_gesture_.GetType());
}

TEST_P(FlingControllerTest, GestureFlingCancelsFiltered) {
  // GFC without previous GFS is dropped.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchscreen);
  EXPECT_FALSE(FlingInProgress());

  // GFC after previous GFS is filtered by fling booster.
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  SimulateFlingCancel(blink::WebGestureDevice::kTouchscreen);
  EXPECT_TRUE(FlingInProgress());

  // Any other GFC while the fling cancelation is deferred gets filtered.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchscreen);
  EXPECT_TRUE(FlingInProgress());
}

TEST_P(FlingControllerTest, GestureFlingNotCancelledBySmallTimeDelta) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0), false);
  EXPECT_TRUE(FlingInProgress());
  int current_sent_scroll_gesture_count = sent_scroll_gesture_count_;

  // If we the first progress tick happens too close to the fling_start time,
  // the controller won't send any GSU events, but the fling is still active.
  ProgressFling(NowTicks());
  EXPECT_EQ(current_sent_scroll_gesture_count, sent_scroll_gesture_count_);
  EXPECT_TRUE(FlingInProgress());

  // The rest of the progress flings must advance the fling normally.
  AdvanceTime();
  ProgressFling(NowTicks());
  EXPECT_EQ(blink::WebGestureDevice::kTouchscreen,
            last_sent_gesture_.SourceDevice());
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_EQ(WebGestureEvent::kMomentumPhase,
            last_sent_gesture_.data.scroll_update.inertial_phase);
  EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);
}

TEST_P(FlingControllerTest, GestureFlingWithNegativeTimeDelta) {
  base::TimeTicks initial_time = NowTicks();
  AdvanceTime();
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  int current_sent_scroll_gesture_count = sent_scroll_gesture_count_;

  // If we get a negative time delta, that is, the Progress tick time happens
  // before the fling's start time then we should *not* try progressing the
  // fling.
  ProgressFling(initial_time);
  EXPECT_EQ(current_sent_scroll_gesture_count, sent_scroll_gesture_count_);

  // The rest of the progress flings must advance the fling normally.
  AdvanceTime();
  ProgressFling(NowTicks());
  EXPECT_EQ(blink::WebGestureDevice::kTouchscreen,
            last_sent_gesture_.SourceDevice());
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_EQ(WebGestureEvent::kMomentumPhase,
            last_sent_gesture_.data.scroll_update.inertial_phase);
  EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);
}

// Regression test for https://crbug.com/924279
TEST_P(FlingControllerTest, TouchpadFlingWithOldEvent) {
  // Only the code path that uses compositor animation observers is affected.
  if (NeedsBeginFrameForFlingProgress())
    return;

  // Create a fling start event.
  base::TimeTicks event_time = NowTicks();
  WebGestureEvent fling_start(WebInputEvent::kGestureFlingStart, 0, event_time,
                              blink::WebGestureDevice::kTouchpad);
  fling_start.data.fling_start.velocity_x = 0.f;
  fling_start.data.fling_start.velocity_y = -1000.f;
  GestureEventWithLatencyInfo fling_start_with_latency(fling_start);

  // Move time forward. Assume a frame occurs here.
  AdvanceTime(1.f);
  base::TimeTicks last_frame_time = NowTicks();

  // Start the fling animation later, as if there was a delay in event dispatch.
  AdvanceTime(1.f);
  fling_controller_->ProcessGestureFlingStart(fling_start_with_latency);
  EXPECT_TRUE(FlingInProgress());

  // Initial scroll was sent.
  EXPECT_EQ(1, wheel_event_count_);
  wheel_event_count_ = 0;

  // Move time forward a little.
  AdvanceTime(1.f);

  // Simulate the compositor animation observer calling ProgressFling with the
  // last frame time. That frame time is after the event time, but before the
  // animation start time.
  ProgressFling(last_frame_time);

  // No scrolls were sent.
  EXPECT_EQ(0, wheel_event_count_);

  // Additional ProgressFling calls generate scroll events as normal.
  AdvanceTime();
  ProgressFling(NowTicks());
  EXPECT_GT(wheel_event_count_, 0);
}

#if defined(OS_LINUX)
#define MAYBE_ControllerBoostsTouchpadFling \
  DISABLED_ControllerBoostsTouchpadFling
#else
#define MAYBE_ControllerBoostsTouchpadFling ControllerBoostsTouchpadFling
#endif
TEST_P(FlingControllerTest, MAYBE_ControllerBoostsTouchpadFling) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchpad,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  // Processing GFS will send the first fling progress event if the time delta
  // between the timestamp of the GFS and the time that ProcessGestureFlingStart
  // is called is large enough.
  bool process_GFS_sent_first_event = first_wheel_event_sent_;

  AdvanceTime();
  ProgressFling(NowTicks());
  if (!process_GFS_sent_first_event) {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseBegan, last_sent_wheel_.momentum_phase);
  } else {
    EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged,
              last_sent_wheel_.momentum_phase);
  }
  EXPECT_GT(last_sent_wheel_.delta_x, 0.f);

  // The rest of the wheel events must have momentum_phase == KPhaseChanged.
  AdvanceTime();
  ProgressFling(NowTicks());
  EXPECT_EQ(WebMouseWheelEvent::kPhaseChanged, last_sent_wheel_.momentum_phase);
  EXPECT_GT(last_sent_wheel_.delta_x, 0.f);

  // Now cancel the fling. The GFC will get suppressed by fling booster.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchpad);
  EXPECT_TRUE(FlingInProgress());

  // The second GFS will boost the current active fling.
  SimulateFlingStart(blink::WebGestureDevice::kTouchpad,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  EXPECT_TRUE(FlingBoosted());
}

TEST_P(FlingControllerTest, ControllerBoostsTouchscreenFling) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  // Fling progress must send GSU events.
  AdvanceTime();
  ProgressFling(NowTicks());
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_EQ(WebGestureEvent::kMomentumPhase,
            last_sent_gesture_.data.scroll_update.inertial_phase);
  EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);

  // Now cancel the fling. The GFC will get suppressed by fling booster.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchscreen);
  EXPECT_TRUE(FlingInProgress());

  // The second GFS will boost the current active fling.
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());
  EXPECT_TRUE(FlingBoosted());
}

TEST_P(FlingControllerTest, ControllerNotifiesTheClientAfterFlingStart) {
  SimulateFlingStart(blink::WebGestureDevice::kTouchscreen,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());

  // Now cancel the fling. The GFC will get suppressed by fling booster.
  SimulateFlingCancel(blink::WebGestureDevice::kTouchscreen);
  EXPECT_TRUE(FlingInProgress());

  // Wait for the boosting timer to expire. The delayed cancelation must work
  // and the client must be notified after fling cancelation.
  AdvanceTime(500);
  ProgressFling(NowTicks());
  EXPECT_FALSE(FlingInProgress());
  EXPECT_EQ(WebInputEvent::kGestureScrollEnd, last_sent_gesture_.GetType());
  EXPECT_TRUE(notified_client_after_fling_stop_);
}

TEST_P(FlingControllerTest, MiddleClickAutoScrollFling) {
  SimulateFlingStart(blink::WebGestureDevice::kSyntheticAutoscroll,
                     gfx::Vector2dF(1000, 0));
  EXPECT_TRUE(FlingInProgress());

  AdvanceTime();
  ProgressFling(NowTicks());
  ASSERT_EQ(WebInputEvent::kGestureScrollUpdate, last_sent_gesture_.GetType());
  EXPECT_EQ(WebGestureEvent::kMomentumPhase,
            last_sent_gesture_.data.scroll_update.inertial_phase);
  EXPECT_GT(last_sent_gesture_.data.scroll_update.delta_x, 0.f);

  // Now send a new fling with different velocity and without sending a fling
  // cancel event, the new fling should always replace the old one even when
  // they are in the same direction.
  SimulateFlingStart(blink::WebGestureDevice::kSyntheticAutoscroll,
                     gfx::Vector2dF(2000, 0));
  EXPECT_TRUE(FlingInProgress());
  EXPECT_FALSE(FlingBoosted());

  // Now cancel the fling. The GFC won't get suppressed by fling booster since
  // autoscroll fling doesn't have boosting.
  SimulateFlingCancel(blink::WebGestureDevice::kSyntheticAutoscroll);
  EXPECT_FALSE(FlingInProgress());
}

}  // namespace content
