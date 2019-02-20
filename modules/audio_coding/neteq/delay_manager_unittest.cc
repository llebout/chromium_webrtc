/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Unit tests for DelayManager class.

#include "modules/audio_coding/neteq/delay_manager.h"

#include <math.h>

#include "modules/audio_coding/neteq/mock/mock_delay_peak_detector.h"
#include "rtc_base/checks.h"
#include "test/field_trial.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::Return;
using ::testing::_;

class DelayManagerTest : public ::testing::Test {
 protected:
  static const int kMaxNumberOfPackets = 240;
  static const int kMinDelayMs = 0;
  static const int kTimeStepMs = 10;
  static const int kFs = 8000;
  static const int kFrameSizeMs = 20;
  static const int kTsIncrement = kFrameSizeMs * kFs / 1000;
  static const int kMaxBufferSizeMs = kMaxNumberOfPackets * kFrameSizeMs;

  DelayManagerTest();
  virtual void SetUp();
  virtual void TearDown();
  void RecreateDelayManager();
  void SetPacketAudioLength(int lengt_ms);
  void InsertNextPacket();
  void IncreaseTime(int inc_ms);

  std::unique_ptr<DelayManager> dm_;
  TickTimer tick_timer_;
  MockDelayPeakDetector detector_;
  uint16_t seq_no_;
  uint32_t ts_;
  bool enable_rtx_handling_ = false;
};

DelayManagerTest::DelayManagerTest()
    : dm_(nullptr),
      detector_(&tick_timer_, false),
      seq_no_(0x1234),
      ts_(0x12345678) {}

void DelayManagerTest::SetUp() {
  RecreateDelayManager();
}

void DelayManagerTest::RecreateDelayManager() {
  EXPECT_CALL(detector_, Reset()).Times(1);
  dm_.reset(new DelayManager(kMaxNumberOfPackets, kMinDelayMs,
                             enable_rtx_handling_, &detector_, &tick_timer_));
}

void DelayManagerTest::SetPacketAudioLength(int lengt_ms) {
  EXPECT_CALL(detector_, SetPacketAudioLength(lengt_ms));
  dm_->SetPacketAudioLength(lengt_ms);
}

void DelayManagerTest::InsertNextPacket() {
  EXPECT_EQ(0, dm_->Update(seq_no_, ts_, kFs));
  seq_no_ += 1;
  ts_ += kTsIncrement;
}

void DelayManagerTest::IncreaseTime(int inc_ms) {
  for (int t = 0; t < inc_ms; t += kTimeStepMs) {
    tick_timer_.Increment();
  }
}
void DelayManagerTest::TearDown() {
  EXPECT_CALL(detector_, Die());
}

TEST_F(DelayManagerTest, CreateAndDestroy) {
  // Nothing to do here. The test fixture creates and destroys the DelayManager
  // object.
}

TEST_F(DelayManagerTest, VectorInitialization) {
  const DelayManager::IATVector& vec = dm_->iat_vector();
  double sum = 0.0;
  for (size_t i = 0; i < vec.size(); i++) {
    EXPECT_NEAR(ldexp(pow(0.5, static_cast<int>(i + 1)), 30), vec[i], 65537);
    // Tolerance 65537 in Q30 corresponds to a delta of approximately 0.00006.
    sum += vec[i];
  }
  EXPECT_EQ(1 << 30, static_cast<int>(sum));  // Should be 1 in Q30.
}

TEST_F(DelayManagerTest, SetPacketAudioLength) {
  const int kLengthMs = 30;
  // Expect DelayManager to pass on the new length to the detector object.
  EXPECT_CALL(detector_, SetPacketAudioLength(kLengthMs)).Times(1);
  EXPECT_EQ(0, dm_->SetPacketAudioLength(kLengthMs));
  EXPECT_EQ(-1, dm_->SetPacketAudioLength(-1));  // Illegal parameter value.
}

TEST_F(DelayManagerTest, PeakFound) {
  // Expect DelayManager to pass on the question to the detector.
  // Call twice, and let the detector return true the first time and false the
  // second time.
  EXPECT_CALL(detector_, peak_found())
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_TRUE(dm_->PeakFound());
  EXPECT_FALSE(dm_->PeakFound());
}

TEST_F(DelayManagerTest, UpdateNormal) {
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Advance time by one frame size.
  IncreaseTime(kFrameSizeMs);
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet, and (base) target level equal to 1 as well.
  // Return false to indicate no peaks found.
  EXPECT_CALL(detector_, Update(1, false, 1)).WillOnce(Return(false));
  InsertNextPacket();
  EXPECT_EQ(1 << 8, dm_->TargetLevel());  // In Q8.
  EXPECT_EQ(1, dm_->base_target_level());
  int lower, higher;
  dm_->BufferLimits(&lower, &higher);
  // Expect |lower| to be 75% of target level, and |higher| to be target level,
  // but also at least 20 ms higher than |lower|, which is the limiting case
  // here.
  EXPECT_EQ((1 << 8) * 3 / 4, lower);
  EXPECT_EQ(lower + (20 << 8) / kFrameSizeMs, higher);
}

TEST_F(DelayManagerTest, UpdateLongInterArrivalTime) {
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Advance time by two frame size.
  IncreaseTime(2 * kFrameSizeMs);
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet, and (base) target level equal to 1 as well.
  // Return false to indicate no peaks found.
  EXPECT_CALL(detector_, Update(2, false, 2)).WillOnce(Return(false));
  InsertNextPacket();
  EXPECT_EQ(2 << 8, dm_->TargetLevel());  // In Q8.
  EXPECT_EQ(2, dm_->base_target_level());
  int lower, higher;
  dm_->BufferLimits(&lower, &higher);
  // Expect |lower| to be 75% of target level, and |higher| to be target level,
  // but also at least 20 ms higher than |lower|, which is the limiting case
  // here.
  EXPECT_EQ((2 << 8) * 3 / 4, lower);
  EXPECT_EQ(lower + (20 << 8) / kFrameSizeMs, higher);
}

TEST_F(DelayManagerTest, UpdatePeakFound) {
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Advance time by one frame size.
  IncreaseTime(kFrameSizeMs);
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet, and (base) target level equal to 1 as well.
  // Return true to indicate that peaks are found. Let the peak height be 5.
  EXPECT_CALL(detector_, Update(1, false, 1)).WillOnce(Return(true));
  EXPECT_CALL(detector_, MaxPeakHeight()).WillOnce(Return(5));
  InsertNextPacket();
  EXPECT_EQ(5 << 8, dm_->TargetLevel());
  EXPECT_EQ(1, dm_->base_target_level());  // Base target level is w/o peaks.
  int lower, higher;
  dm_->BufferLimits(&lower, &higher);
  // Expect |lower| to be 75% of target level, and |higher| to be target level.
  EXPECT_EQ((5 << 8) * 3 / 4, lower);
  EXPECT_EQ(5 << 8, higher);
}

TEST_F(DelayManagerTest, TargetDelay) {
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Advance time by one frame size.
  IncreaseTime(kFrameSizeMs);
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet, and (base) target level equal to 1 as well.
  // Return false to indicate no peaks found.
  EXPECT_CALL(detector_, Update(1, false, 1)).WillOnce(Return(false));
  InsertNextPacket();
  const int kExpectedTarget = 1;
  EXPECT_EQ(kExpectedTarget << 8, dm_->TargetLevel());  // In Q8.
  EXPECT_EQ(1, dm_->base_target_level());
  int lower, higher;
  dm_->BufferLimits(&lower, &higher);
  // Expect |lower| to be 75% of base target level, and |higher| to be
  // lower + 20 ms headroom.
  EXPECT_EQ((1 << 8) * 3 / 4, lower);
  EXPECT_EQ(lower + (20 << 8) / kFrameSizeMs, higher);
}

TEST_F(DelayManagerTest, MaxDelay) {
  const int kExpectedTarget = 5;
  const int kTimeIncrement = kExpectedTarget * kFrameSizeMs;
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to |kExpectedTarget| packet. Return true to indicate peaks found.
  EXPECT_CALL(detector_, Update(kExpectedTarget, false, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(detector_, MaxPeakHeight())
      .WillRepeatedly(Return(kExpectedTarget));
  IncreaseTime(kTimeIncrement);
  InsertNextPacket();

  // No limit is set.
  EXPECT_EQ(kExpectedTarget << 8, dm_->TargetLevel());

  int kMaxDelayPackets = kExpectedTarget - 2;
  int kMaxDelayMs = kMaxDelayPackets * kFrameSizeMs;
  EXPECT_TRUE(dm_->SetMaximumDelay(kMaxDelayMs));
  IncreaseTime(kTimeIncrement);
  InsertNextPacket();
  EXPECT_EQ(kMaxDelayPackets << 8, dm_->TargetLevel());

  // Target level at least should be one packet.
  EXPECT_FALSE(dm_->SetMaximumDelay(kFrameSizeMs - 1));
}

TEST_F(DelayManagerTest, MinDelay) {
  const int kExpectedTarget = 5;
  const int kTimeIncrement = kExpectedTarget * kFrameSizeMs;
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to |kExpectedTarget| packet. Return true to indicate peaks found.
  EXPECT_CALL(detector_, Update(kExpectedTarget, false, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(detector_, MaxPeakHeight())
      .WillRepeatedly(Return(kExpectedTarget));
  IncreaseTime(kTimeIncrement);
  InsertNextPacket();

  // No limit is applied.
  EXPECT_EQ(kExpectedTarget << 8, dm_->TargetLevel());

  int kMinDelayPackets = kExpectedTarget + 2;
  int kMinDelayMs = kMinDelayPackets * kFrameSizeMs;
  dm_->SetMinimumDelay(kMinDelayMs);
  IncreaseTime(kTimeIncrement);
  InsertNextPacket();
  EXPECT_EQ(kMinDelayPackets << 8, dm_->TargetLevel());
}

TEST_F(DelayManagerTest, BaseMinimumDelayCheckValidRange) {
  SetPacketAudioLength(kFrameSizeMs);

  // Base minimum delay should be between [0, 10000] milliseconds.
  EXPECT_FALSE(dm_->SetBaseMinimumDelay(-1));
  EXPECT_FALSE(dm_->SetBaseMinimumDelay(10001));
  EXPECT_EQ(dm_->GetBaseMinimumDelay(), 0);

  EXPECT_TRUE(dm_->SetBaseMinimumDelay(7999));
  EXPECT_EQ(dm_->GetBaseMinimumDelay(), 7999);
}

TEST_F(DelayManagerTest, BaseMinimumDelayLowerThanMinimumDelay) {
  SetPacketAudioLength(kFrameSizeMs);
  constexpr int kBaseMinimumDelayMs = 100;
  constexpr int kMinimumDelayMs = 200;

  // Base minimum delay sets lower bound on minimum. That is why when base
  // minimum delay is lower than minimum delay we use minimum delay.
  RTC_DCHECK_LT(kBaseMinimumDelayMs, kMinimumDelayMs);

  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));
  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kMinimumDelayMs);
}

TEST_F(DelayManagerTest, BaseMinimumDelayGreaterThanMinimumDelay) {
  SetPacketAudioLength(kFrameSizeMs);
  constexpr int kBaseMinimumDelayMs = 70;
  constexpr int kMinimumDelayMs = 30;

  // Base minimum delay sets lower bound on minimum. That is why when base
  // minimum delay is greater than minimum delay we use base minimum delay.
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMinimumDelayMs);

  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));
  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kBaseMinimumDelayMs);
}

TEST_F(DelayManagerTest, BaseMinimumDelayGreaterThanBufferSize) {
  SetPacketAudioLength(kFrameSizeMs);
  constexpr int kBaseMinimumDelayMs = kMaxBufferSizeMs + 1;
  constexpr int kMinimumDelayMs = 12;
  constexpr int kMaximumDelayMs = 20;
  constexpr int kMaxBufferSizeMsQ75 = 3 * kMaxBufferSizeMs / 4;

  EXPECT_TRUE(dm_->SetMaximumDelay(kMaximumDelayMs));

  // Base minimum delay is greater than minimum delay, that is why we clamp
  // it to current the highest possible value which is maximum delay.
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMinimumDelayMs);
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMaxBufferSizeMs);
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMaximumDelayMs);
  RTC_DCHECK_LT(kMaximumDelayMs, kMaxBufferSizeMsQ75);

  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));

  // Unset maximum value.
  EXPECT_TRUE(dm_->SetMaximumDelay(0));

  // With maximum value unset, the highest possible value now is 75% of
  // currently possible maximum buffer size.
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kMaxBufferSizeMsQ75);
}

TEST_F(DelayManagerTest, BaseMinimumDelayGreaterThanMaximumDelay) {
  SetPacketAudioLength(kFrameSizeMs);
  constexpr int kMaximumDelayMs = 400;
  constexpr int kBaseMinimumDelayMs = kMaximumDelayMs + 1;
  constexpr int kMinimumDelayMs = 20;

  // Base minimum delay is greater than minimum delay, that is why we clamp
  // it to current the highest possible value which is kMaximumDelayMs.
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMinimumDelayMs);
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMaximumDelayMs);
  RTC_DCHECK_LT(kMaximumDelayMs, kMaxBufferSizeMs);

  EXPECT_TRUE(dm_->SetMaximumDelay(kMaximumDelayMs));
  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kMaximumDelayMs);
}

TEST_F(DelayManagerTest, BaseMinimumDelayLowerThanMaxSize) {
  SetPacketAudioLength(kFrameSizeMs);
  constexpr int kMaximumDelayMs = 400;
  constexpr int kBaseMinimumDelayMs = kMaximumDelayMs - 1;
  constexpr int kMinimumDelayMs = 20;

  // Base minimum delay is greater than minimum delay, and lower than maximum
  // delays that is why it is used.
  RTC_DCHECK_GT(kBaseMinimumDelayMs, kMinimumDelayMs);
  RTC_DCHECK_LT(kBaseMinimumDelayMs, kMaximumDelayMs);

  EXPECT_TRUE(dm_->SetMaximumDelay(kMaximumDelayMs));
  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kBaseMinimumDelayMs);
}

TEST_F(DelayManagerTest, MinimumDelayMemorization) {
  // Check that when we increase base minimum delay to value higher than
  // minimum delay then minimum delay is still memorized. This allows to
  // restore effective minimum delay to memorized minimum delay value when we
  // decrease base minimum delay.
  SetPacketAudioLength(kFrameSizeMs);

  constexpr int kBaseMinimumDelayMsLow = 10;
  constexpr int kMinimumDelayMs = 20;
  constexpr int kBaseMinimumDelayMsHigh = 30;

  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMsLow));
  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  // Minimum delay is used as it is higher than base minimum delay.
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kMinimumDelayMs);

  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMsHigh));
  // Base minimum delay is used as it is now higher than minimum delay.
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(),
            kBaseMinimumDelayMsHigh);

  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMsLow));
  // Check that minimum delay is memorized and is used again.
  EXPECT_EQ(dm_->effective_minimum_delay_ms_for_test(), kMinimumDelayMs);
}

TEST_F(DelayManagerTest, BaseMinimumDelay) {
  const int kExpectedTarget = 5;
  const int kTimeIncrement = kExpectedTarget * kFrameSizeMs;
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to |kExpectedTarget| packet. Return true to indicate peaks found.
  EXPECT_CALL(detector_, Update(kExpectedTarget, false, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(detector_, MaxPeakHeight())
      .WillRepeatedly(Return(kExpectedTarget));
  IncreaseTime(kTimeIncrement);
  InsertNextPacket();

  // No limit is applied.
  EXPECT_EQ(kExpectedTarget << 8, dm_->TargetLevel());

  constexpr int kBaseMinimumDelayPackets = kExpectedTarget + 2;
  constexpr int kBaseMinimumDelayMs = kBaseMinimumDelayPackets * kFrameSizeMs;
  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));
  EXPECT_EQ(dm_->GetBaseMinimumDelay(), kBaseMinimumDelayMs);

  IncreaseTime(kTimeIncrement);
  InsertNextPacket();
  EXPECT_EQ(dm_->GetBaseMinimumDelay(), kBaseMinimumDelayMs);
  EXPECT_EQ(kBaseMinimumDelayPackets << 8, dm_->TargetLevel());
}

TEST_F(DelayManagerTest, BaseMinimumDealyAffectTargetLevel) {
  const int kExpectedTarget = 5;
  const int kTimeIncrement = kExpectedTarget * kFrameSizeMs;
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to |kExpectedTarget|. Return true to indicate peaks found.
  EXPECT_CALL(detector_, Update(kExpectedTarget, false, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(detector_, MaxPeakHeight())
      .WillRepeatedly(Return(kExpectedTarget));
  IncreaseTime(kTimeIncrement);
  InsertNextPacket();

  // No limit is applied.
  EXPECT_EQ(kExpectedTarget << 8, dm_->TargetLevel());

  // Minimum delay is lower than base minimum delay, that is why base minimum
  // delay is used to calculate target level.
  constexpr int kMinimumDelayPackets = kExpectedTarget + 1;
  constexpr int kBaseMinimumDelayPackets = kExpectedTarget + 2;

  constexpr int kMinimumDelayMs = kMinimumDelayPackets * kFrameSizeMs;
  constexpr int kBaseMinimumDelayMs = kBaseMinimumDelayPackets * kFrameSizeMs;

  EXPECT_TRUE(kMinimumDelayMs < kBaseMinimumDelayMs);
  EXPECT_TRUE(dm_->SetMinimumDelay(kMinimumDelayMs));
  EXPECT_TRUE(dm_->SetBaseMinimumDelay(kBaseMinimumDelayMs));
  EXPECT_EQ(dm_->GetBaseMinimumDelay(), kBaseMinimumDelayMs);

  IncreaseTime(kTimeIncrement);
  InsertNextPacket();
  EXPECT_EQ(dm_->GetBaseMinimumDelay(), kBaseMinimumDelayMs);
  EXPECT_EQ(kBaseMinimumDelayPackets << 8, dm_->TargetLevel());
}

TEST_F(DelayManagerTest, UpdateReorderedPacket) {
  SetPacketAudioLength(kFrameSizeMs);
  InsertNextPacket();

  // Insert packet that was sent before the previous packet.
  EXPECT_CALL(detector_, Update(_, true, _));
  EXPECT_EQ(0, dm_->Update(seq_no_ - 1, ts_ - kFrameSizeMs, kFs));
}

TEST_F(DelayManagerTest, EnableRtxHandling) {
  enable_rtx_handling_ = true;
  RecreateDelayManager();

  // Insert first packet.
  SetPacketAudioLength(kFrameSizeMs);
  InsertNextPacket();

  // Insert reordered packet.
  // TODO(jakobi): Test estimated inter-arrival time by mocking the histogram
  // instead of checking the call to the peak detector.
  EXPECT_CALL(detector_, Update(3, true, _));
  EXPECT_EQ(0, dm_->Update(seq_no_ - 3, ts_ - 3 * kFrameSizeMs, kFs));

  // Insert another reordered packet.
  EXPECT_CALL(detector_, Update(2, true, _));
  EXPECT_EQ(0, dm_->Update(seq_no_ - 2, ts_ - 2 * kFrameSizeMs, kFs));

  // Insert the next packet in order and verify that the inter-arrival time is
  // estimated correctly.
  IncreaseTime(kFrameSizeMs);
  EXPECT_CALL(detector_, Update(1, false, _));
  InsertNextPacket();
}

// Tests that skipped sequence numbers (simulating empty packets) are handled
// correctly.
TEST_F(DelayManagerTest, EmptyPacketsReported) {
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();

  // Advance time by one frame size.
  IncreaseTime(kFrameSizeMs);

  // Advance the sequence number by 5, simulating that 5 empty packets were
  // received, but never inserted.
  seq_no_ += 10;
  for (int j = 0; j < 10; ++j) {
    dm_->RegisterEmptyPacket();
  }

  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet, and (base) target level equal to 1 as well.
  // Return false to indicate no peaks found.
  EXPECT_CALL(detector_, Update(1, false, 1)).WillOnce(Return(false));
  InsertNextPacket();

  EXPECT_EQ(1 << 8, dm_->TargetLevel());  // In Q8.
}

// Same as above, but do not call RegisterEmptyPacket. Observe the target level
// increase dramatically.
TEST_F(DelayManagerTest, EmptyPacketsNotReported) {
  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();

  // Advance time by one frame size.
  IncreaseTime(kFrameSizeMs);

  // Advance the sequence number by 5, simulating that 5 empty packets were
  // received, but never inserted.
  seq_no_ += 10;

  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet, and (base) target level equal to 1 as well.
  // Return false to indicate no peaks found.
  EXPECT_CALL(detector_, Update(10, false, 10)).WillOnce(Return(false));
  InsertNextPacket();

  // Note 10 times higher target value.
  EXPECT_EQ(10 * 1 << 8, dm_->TargetLevel());  // In Q8.
}

TEST_F(DelayManagerTest, Failures) {
  // Wrong sample rate.
  EXPECT_EQ(-1, dm_->Update(0, 0, -1));
  // Wrong packet size.
  EXPECT_EQ(-1, dm_->SetPacketAudioLength(0));
  EXPECT_EQ(-1, dm_->SetPacketAudioLength(-1));

  // Minimum delay higher than a maximum delay is not accepted.
  EXPECT_TRUE(dm_->SetMaximumDelay(10));
  EXPECT_FALSE(dm_->SetMinimumDelay(20));

  // Maximum delay less than minimum delay is not accepted.
  EXPECT_TRUE(dm_->SetMaximumDelay(100));
  EXPECT_TRUE(dm_->SetMinimumDelay(80));
  EXPECT_FALSE(dm_->SetMaximumDelay(60));
}

TEST_F(DelayManagerTest, TargetDelayGreaterThanOne) {
  test::ScopedFieldTrials field_trial(
      "WebRTC-Audio-NetEqForceTargetDelayPercentile/Enabled-0/");
  RecreateDelayManager();
  EXPECT_EQ(absl::make_optional<int>(1 << 30),
            dm_->forced_limit_probability_for_test());

  SetPacketAudioLength(kFrameSizeMs);
  // First packet arrival.
  InsertNextPacket();
  // Advance time by one frame size.
  IncreaseTime(kFrameSizeMs);
  // Second packet arrival.
  // Expect detector update method to be called once with inter-arrival time
  // equal to 1 packet.
  EXPECT_CALL(detector_, Update(1, false, 1)).WillOnce(Return(false));
  InsertNextPacket();
  constexpr int kExpectedTarget = 1;
  EXPECT_EQ(kExpectedTarget << 8, dm_->TargetLevel());  // In Q8.
}

TEST_F(DelayManagerTest, ForcedTargetDelayPercentile) {
  {
    test::ScopedFieldTrials field_trial(
        "WebRTC-Audio-NetEqForceTargetDelayPercentile/Enabled-95/");
    RecreateDelayManager();
    EXPECT_EQ(absl::make_optional<int>(53687091),
              dm_->forced_limit_probability_for_test());  // 1/20 in Q30
  }
  {
    test::ScopedFieldTrials field_trial(
        "WebRTC-Audio-NetEqForceTargetDelayPercentile/Enabled-99.95/");
    RecreateDelayManager();
    EXPECT_EQ(absl::make_optional<int>(536871),
              dm_->forced_limit_probability_for_test());  // 1/2000 in Q30
  }
  {
    test::ScopedFieldTrials field_trial(
        "WebRTC-Audio-NetEqForceTargetDelayPercentile/Disabled/");
    RecreateDelayManager();
    EXPECT_EQ(absl::nullopt, dm_->forced_limit_probability_for_test());
  }
  {
    test::ScopedFieldTrials field_trial(
        "WebRTC-Audio-NetEqForceTargetDelayPercentile/Enabled--1/");
    EXPECT_EQ(absl::nullopt, dm_->forced_limit_probability_for_test());
  }
  {
    test::ScopedFieldTrials field_trial(
        "WebRTC-Audio-NetEqForceTargetDelayPercentile/Enabled-100.1/");
    RecreateDelayManager();
    EXPECT_EQ(absl::nullopt, dm_->forced_limit_probability_for_test());
  }
}

// Test if the histogram is stretched correctly if the packet size is decreased.
TEST(DelayManagerIATScalingTest, StretchTest) {
  using IATVector = DelayManager::IATVector;
  // Test a straightforward 60ms to 20ms change.
  IATVector iat = {12, 0, 0, 0, 0, 0};
  IATVector expected_result = {4, 4, 4, 0, 0, 0};
  IATVector stretched_iat = DelayManager::ScaleHistogram(iat, 60, 20);
  EXPECT_EQ(stretched_iat, expected_result);

  // Test an example where the last bin in the stretched histogram should
  // contain the sum of the elements that don't fit into the new histogram.
  iat = {18, 15, 12, 9, 6, 3, 0};
  expected_result = {6, 6, 6, 5, 5, 5, 30};
  stretched_iat = DelayManager::ScaleHistogram(iat, 60, 20);
  EXPECT_EQ(stretched_iat, expected_result);

  // Test a 120ms to 60ms change.
  iat = {18, 16, 14, 4, 0};
  expected_result = {9, 9, 8, 8, 18};
  stretched_iat = DelayManager::ScaleHistogram(iat, 120, 60);
  EXPECT_EQ(stretched_iat, expected_result);

  // Test a 120ms to 20ms change.
  iat = {19, 12, 0, 0, 0, 0, 0, 0};
  expected_result = {3, 3, 3, 3, 3, 3, 2, 11};
  stretched_iat = DelayManager::ScaleHistogram(iat, 120, 20);
  EXPECT_EQ(stretched_iat, expected_result);

  // Test a 70ms to 40ms change.
  iat = {13, 7, 5, 3, 1, 5, 12, 11, 3, 0, 0, 0};
  expected_result = {7, 5, 5, 3, 3, 2, 2, 1, 2, 2, 6, 22};
  stretched_iat = DelayManager::ScaleHistogram(iat, 70, 40);
  EXPECT_EQ(stretched_iat, expected_result);

  // Test a 30ms to 20ms change.
  iat = {13, 7, 5, 3, 1, 5, 12, 11, 3, 0, 0, 0};
  expected_result = {8, 6, 6, 3, 2, 2, 1, 3, 3, 8, 7, 11};
  stretched_iat = DelayManager::ScaleHistogram(iat, 30, 20);
  EXPECT_EQ(stretched_iat, expected_result);
}

// Test if the histogram is compressed correctly if the packet size is
// increased.
TEST(DelayManagerIATScalingTest, CompressionTest) {
  using IATVector = DelayManager::IATVector;
  // Test a 20 to 60 ms change.
  IATVector iat = {12, 11, 10, 3, 2, 1};
  IATVector expected_result = {33, 6, 0, 0, 0, 0};
  IATVector compressed_iat = DelayManager::ScaleHistogram(iat, 20, 60);
  EXPECT_EQ(compressed_iat, expected_result);

  // Test a 60ms to 120ms change.
  iat = {18, 16, 14, 4, 1};
  expected_result = {34, 18, 1, 0, 0};
  compressed_iat = DelayManager::ScaleHistogram(iat, 60, 120);
  EXPECT_EQ(compressed_iat, expected_result);

  // Test a 20ms to 120ms change.
  iat = {18, 12, 5, 4, 4, 3, 5, 1};
  expected_result = {46, 6, 0, 0, 0, 0, 0, 0};
  compressed_iat = DelayManager::ScaleHistogram(iat, 20, 120);
  EXPECT_EQ(compressed_iat, expected_result);

  // Test a 70ms to 80ms change.
  iat = {13, 7, 5, 3, 1, 5, 12, 11, 3};
  expected_result = {11, 8, 6, 2, 5, 12, 13, 3, 0};
  compressed_iat = DelayManager::ScaleHistogram(iat, 70, 80);
  EXPECT_EQ(compressed_iat, expected_result);

  // Test a 50ms to 110ms change.
  iat = {13, 7, 5, 3, 1, 5, 12, 11, 3};
  expected_result = {18, 8, 16, 16, 2, 0, 0, 0, 0};
  compressed_iat = DelayManager::ScaleHistogram(iat, 50, 110);
  EXPECT_EQ(compressed_iat, expected_result);
}

// Test if the histogram scaling function handles overflows correctly.
TEST(DelayManagerIATScalingTest, OverflowTest) {
  using IATVector = DelayManager::IATVector;
  // Test a compression operation that can cause overflow.
  IATVector iat = {733544448, 0, 0, 0, 0, 0, 0, 340197376, 0, 0, 0, 0, 0, 0};
  IATVector expected_result = {733544448, 340197376, 0, 0, 0, 0, 0,
                               0,         0,         0, 0, 0, 0, 0};
  IATVector scaled_iat = DelayManager::ScaleHistogram(iat, 10, 60);
  EXPECT_EQ(scaled_iat, expected_result);

  iat = {655591163, 39962288, 360736736, 1930514, 4003853, 1782764,
         114119,    2072996,  0,         2149354, 0};
  expected_result = {1056290187, 7717131, 2187115, 2149354, 0, 0,
                     0,          0,       0,       0,       0};
  scaled_iat = DelayManager::ScaleHistogram(iat, 20, 60);
  EXPECT_EQ(scaled_iat, expected_result);

  // In this test case we will not be able to add everything to the final bin in
  // the scaled histogram. Check that the last bin doesn't overflow.
  iat = {2000000000, 2000000000, 2000000000,
         2000000000, 2000000000, 2000000000};
  expected_result = {666666666, 666666666, 666666666,
                     666666667, 666666667, 2147483647};
  scaled_iat = DelayManager::ScaleHistogram(iat, 60, 20);
  EXPECT_EQ(scaled_iat, expected_result);

  // In this test case we will not be able to add enough to each of the bins,
  // so the values should be smeared out past the end of the normal range.
  iat = {2000000000, 2000000000, 2000000000,
         2000000000, 2000000000, 2000000000};
  expected_result = {2147483647, 2147483647, 2147483647,
                     2147483647, 2147483647, 1262581765};
  scaled_iat = DelayManager::ScaleHistogram(iat, 20, 60);
  EXPECT_EQ(scaled_iat, expected_result);
}

}  // namespace webrtc
