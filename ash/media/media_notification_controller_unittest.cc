// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/media/media_notification_controller.h"

#include <memory>

#include "ash/media/media_notification_constants.h"
#include "ash/media/media_notification_item.h"
#include "ash/public/cpp/ash_features.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/unguessable_token.h"
#include "services/media_session/public/mojom/audio_focus.mojom.h"
#include "ui/message_center/message_center.h"

namespace ash {

namespace {

media_session::mojom::MediaSessionInfoPtr BuildMediaSessionInfo(
    bool is_controllable) {
  media_session::mojom::MediaSessionInfoPtr session_info(
      media_session::mojom::MediaSessionInfo::New());
  session_info->is_controllable = is_controllable;
  return session_info;
}

media_session::mojom::AudioFocusRequestStatePtr GetRequestStateWithId(
    const base::UnguessableToken& id) {
  media_session::mojom::AudioFocusRequestStatePtr session(
      media_session::mojom::AudioFocusRequestState::New());
  session->request_id = id;
  session->session_info = BuildMediaSessionInfo(true);
  return session;
}

}  // namespace

class MediaNotificationControllerTest : public AshTestBase {
 public:
  MediaNotificationControllerTest() = default;
  ~MediaNotificationControllerTest() override = default;

  // AshTestBase
  void SetUp() override {
    scoped_feature_list_.InitAndEnableFeature(
        features::kMediaSessionNotification);

    AshTestBase::SetUp();
  }

  void ExpectNotificationCount(unsigned count) {
    message_center::MessageCenter* message_center =
        message_center::MessageCenter::Get();

    EXPECT_EQ(count, message_center->GetVisibleNotifications().size());

    // Media notifications should never be shown as a popup so we always check
    // this is empty.
    EXPECT_TRUE(message_center->GetPopupNotifications().empty());
  }

  media_session::MediaMetadata BuildMediaMetadata() {
    media_session::MediaMetadata metadata;
    metadata.title = base::ASCIIToUTF16("title");
    metadata.artist = base::ASCIIToUTF16("artist");
    return metadata;
  }

  void ExpectHistogramCountRecorded(int count, int size) {
    histogram_tester_.ExpectBucketCount(
        MediaNotificationController::kCountHistogramName, count, size);
  }

  void ExpectHistogramSourceRecorded(MediaNotificationItem::Source source) {
    histogram_tester_.ExpectUniqueSample(
        MediaNotificationItem::kSourceHistogramName,
        static_cast<base::HistogramBase::Sample>(source), 1);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

  base::HistogramTester histogram_tester_;

  DISALLOW_COPY_AND_ASSIGN(MediaNotificationControllerTest);
};

// Test toggling the notification multiple times with the same ID. Since the
// notification is keyed by ID we should only ever show one.
TEST_F(MediaNotificationControllerTest, OnFocusGainedLost_SameId) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  Shell::Get()->media_notification_controller()->OnFocusLost(
      GetRequestStateWithId(id));

  ExpectNotificationCount(0);
}

// Test toggling the notification multiple times with different IDs. This should
// show one notification per ID.
TEST_F(MediaNotificationControllerTest, OnFocusGainedLost_MultipleIds) {
  base::UnguessableToken id1 = base::UnguessableToken::Create();
  base::UnguessableToken id2 = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id1));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id1.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id2));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id2.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(2);
  ExpectHistogramCountRecorded(2, 1);

  Shell::Get()->media_notification_controller()->OnFocusLost(
      GetRequestStateWithId(id1));

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);
}

// Test that a notification is hidden when it becomes uncontrollable. We still
// keep the MediaNotificationItem around in case it becomes controllable again.
TEST_F(MediaNotificationControllerTest,
       OnFocusGained_ControllableBecomesUncontrollable) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionInfoChanged(BuildMediaSessionInfo(false));

  ExpectNotificationCount(0);
}

// Test that a notification is shown when it becomes controllable.
TEST_F(MediaNotificationControllerTest,
       OnFocusGained_NotControllableBecomesControllable) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  media_session::mojom::AudioFocusRequestStatePtr state =
      GetRequestStateWithId(id);
  state->session_info->is_controllable = false;
  Shell::Get()->media_notification_controller()->OnFocusGained(
      std::move(state));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(0);

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionInfoChanged(BuildMediaSessionInfo(true));

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);
}

// Test hiding a notification with an invalid ID.
TEST_F(MediaNotificationControllerTest, OnFocusLost_Noop) {
  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusLost(
      GetRequestStateWithId(base::UnguessableToken::Create()));

  ExpectNotificationCount(0);
}

// Test that media notifications have the correct custom view type.
TEST_F(MediaNotificationControllerTest, NotificationHasCustomViewType) {
  ExpectNotificationCount(0);

  base::UnguessableToken id = base::UnguessableToken::Create();

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  message_center::Notification* notification =
      message_center::MessageCenter::Get()->FindVisibleNotificationById(
          id.ToString());
  EXPECT_TRUE(notification);

  EXPECT_EQ(kMediaSessionNotificationCustomViewType,
            notification->custom_view_type());
}

// Test that if we recieve a null media session info that we hide the
// notification.
TEST_F(MediaNotificationControllerTest, HandleNullMediaSessionInfo) {
  ExpectNotificationCount(0);

  base::UnguessableToken id = base::UnguessableToken::Create();

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionInfoChanged(nullptr);

  ExpectNotificationCount(0);
}

TEST_F(MediaNotificationControllerTest, MediaMetadata_NoArtist) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  media_session::MediaMetadata metadata;
  metadata.title = base::ASCIIToUTF16("title");

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(metadata);

  ExpectNotificationCount(0);
}

TEST_F(MediaNotificationControllerTest, MediaMetadata_NoTitle) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  media_session::MediaMetadata metadata;
  metadata.artist = base::ASCIIToUTF16("artist");

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(metadata);

  ExpectNotificationCount(0);
}

TEST_F(MediaNotificationControllerTest, MediaMetadataUpdated_MissingInfo) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramCountRecorded(1, 1);

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(media_session::MediaMetadata());

  ExpectNotificationCount(0);
}

TEST_F(MediaNotificationControllerTest, RecordHistogramSource_Unknown) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  Shell::Get()->media_notification_controller()->OnFocusGained(
      GetRequestStateWithId(id));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramSourceRecorded(MediaNotificationItem::Source::kUnknown);
}

TEST_F(MediaNotificationControllerTest, RecordHistogramSource_Web) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  media_session::mojom::AudioFocusRequestStatePtr request =
      GetRequestStateWithId(id);
  request->source_name = "web";

  Shell::Get()->media_notification_controller()->OnFocusGained(
      std::move(request));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramSourceRecorded(MediaNotificationItem::Source::kWeb);
}

TEST_F(MediaNotificationControllerTest, RecordHistogramSource_Assistant) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  media_session::mojom::AudioFocusRequestStatePtr request =
      GetRequestStateWithId(id);
  request->source_name = "assistant";

  Shell::Get()->media_notification_controller()->OnFocusGained(
      std::move(request));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramSourceRecorded(MediaNotificationItem::Source::kAssistant);
}

TEST_F(MediaNotificationControllerTest, RecordHistogramSource_Arc) {
  base::UnguessableToken id = base::UnguessableToken::Create();

  ExpectNotificationCount(0);

  media_session::mojom::AudioFocusRequestStatePtr request =
      GetRequestStateWithId(id);
  request->source_name = "arc";

  Shell::Get()->media_notification_controller()->OnFocusGained(
      std::move(request));

  Shell::Get()
      ->media_notification_controller()
      ->GetItem(id.ToString())
      ->MediaSessionMetadataChanged(BuildMediaMetadata());

  ExpectNotificationCount(1);
  ExpectHistogramSourceRecorded(MediaNotificationItem::Source::kArc);
}

}  // namespace ash
