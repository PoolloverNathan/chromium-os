// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discod/controls/real_ufs_write_booster_control_logic.h"

#include <base/time/time.h>
#include <brillo/blkdev_utils/disk_iostat.h>
#include <gtest/gtest.h>

#include "discod/controls/fake_binary_control.h"
#include "discod/utils/libhwsec_status_import.h"

namespace discod {
namespace {

class RealUfsWriteBoosterControlLogicTest : public ::testing::Test {
 public:
  RealUfsWriteBoosterControlLogicTest()
      : binary_control_(new FakeBinaryControl()),
        control_logic_(std::unique_ptr<BinaryControl>(binary_control_)) {
    brillo::DiskIoStat::Stat over_stat = {.write_sectors = 75 * 256};
    brillo::DiskIoStat::Stat under_stat = {.write_sectors = 25 * 256};
    over_threshold_delta_ = brillo::DiskIoStat::Delta(
        brillo::DiskIoStat::Snapshot(base::Seconds(1), over_stat));
    under_threshold_delta_ = brillo::DiskIoStat::Delta(
        brillo::DiskIoStat::Snapshot(base::Seconds(1), under_stat));
  }

  ~RealUfsWriteBoosterControlLogicTest() override = default;

 protected:
  FakeBinaryControl* binary_control_;  // owned by control_logic_
  RealUfsWriteBoosterControlLogic control_logic_;

  brillo::DiskIoStat::Delta over_threshold_delta_;
  brillo::DiskIoStat::Delta under_threshold_delta_;
};

}  // namespace

TEST_F(RealUfsWriteBoosterControlLogicTest, ErrorPropagation) {
  // Default false after first Update.
  ASSERT_THAT(control_logic_.Reset(), IsOk());
  EXPECT_FALSE(binary_control_->Current().value());

  for (int i = 0; i < 2; ++i) {
    EXPECT_FALSE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());
  }
  EXPECT_FALSE(binary_control_->Current().value());

  // Inject error
  binary_control_->InjectError("XXX");
  // Expect error instead of state transition.
  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), NotOk());

  EXPECT_FALSE(binary_control_->Current().value());

  // Next update succeeds.
  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());
  EXPECT_TRUE(binary_control_->Current().value());

  // Trigger turn off.
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_FALSE(binary_control_->Current().value());

  // Inject error
  binary_control_->InjectError("XXX");

  // Expect and error on enable
  EXPECT_THAT(control_logic_.Enable(), NotOk());
  EXPECT_FALSE(binary_control_->Current().value());

  // Next one succeeds
  EXPECT_THAT(control_logic_.Enable(), IsOk());
  EXPECT_TRUE(binary_control_->Current().value());
}

TEST_F(RealUfsWriteBoosterControlLogicTest, NoExplicitTrigger) {
  ASSERT_THAT(control_logic_.Reset(), IsOk());
  EXPECT_FALSE(binary_control_->Current().value());

  // Off when under threshold
  for (int i = 0; i < 10; ++i) {
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
    EXPECT_FALSE(binary_control_->Current().value());
  }

  // Off->On counter reset when under threshold
  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());
  EXPECT_FALSE(binary_control_->Current().value());

  ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  EXPECT_FALSE(binary_control_->Current().value());

  for (int i = 0; i < 2; ++i) {
    EXPECT_FALSE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());
  }
  EXPECT_FALSE(binary_control_->Current().value());

  ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  EXPECT_FALSE(binary_control_->Current().value());

  // Off->On when overthreshold for hysteresis period
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());
  }
  EXPECT_TRUE(binary_control_->Current().value());

  // On->Off counter reset when over threshold
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_TRUE(binary_control_->Current().value());

  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());

  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_TRUE(binary_control_->Current().value());

  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());

  // On->Off when under threshold for hysteresis
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_FALSE(binary_control_->Current().value());
}

TEST_F(RealUfsWriteBoosterControlLogicTest, ExplicitTrigger) {
  ASSERT_THAT(control_logic_.Reset(), IsOk());
  EXPECT_FALSE(binary_control_->Current().value());

  EXPECT_THAT(control_logic_.Enable(), IsOk());

  // On->Off counter reset when over threshold
  for (int i = 0; i < 59; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_TRUE(binary_control_->Current().value());

  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());

  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_TRUE(binary_control_->Current().value());

  ASSERT_THAT(control_logic_.Update(over_threshold_delta_), IsOk());

  // On->Off when under threshold for hysteresis, and another enable resets
  // counter.
  for (int i = 0; i < 59; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_TRUE(binary_control_->Current().value());

  EXPECT_THAT(control_logic_.Enable(), IsOk());

  for (int i = 0; i < 60; ++i) {
    EXPECT_TRUE(binary_control_->Current().value());
    ASSERT_THAT(control_logic_.Update(under_threshold_delta_), IsOk());
  }
  EXPECT_FALSE(binary_control_->Current().value());
}

}  // namespace discod