// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/update_device_info_state_handler.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/mock_cbi_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_crossystem_utils.h"
#include "rmad/utils/mock_regions_utils.h"
#include "rmad/utils/mock_vpd_utils.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

constexpr char kSerialNumber[] = "TestSerialNumber";
constexpr char kRegion[] = "TestRegion";
// We get the value of type int from cros_config, but we set uint64_t in cbi.
// This is used to mock the result of cbi, so it is an int.
constexpr uint64_t kSkuId = 1234567890;
// Test for overflow, because we get uint64_t from cbi and int from cros_config
constexpr uint64_t kSkuIdOverflow =
    static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1;
constexpr char kCustomLabelTag[] = "TestCustomLabelTag";
constexpr char kDramPartNum[] = "TestDramPartNum";

const std::vector<std::string> kRegionList = {"TestRegion", "TestRegion1"};
// We get the value of type int from cros_config, but we set uint64_t in cbi.
// This is used to mock the result of cros_config, so it is a vector<int>.
const std::vector<int> kSkuList = {1234567890, 1234567891};
const std::vector<std::string> kCustomLabelTagList = {
    "TestCustomLabelTag", "TestCustomLabelTag0", "TestCustomLabelTag1"};
constexpr uint32_t kOriginalRegionSelection = 0;
constexpr uint32_t kOriginalSkuSelection = 0;
constexpr uint32_t kOriginalCustomLabelSelection = 0;

constexpr char kNewSerialNumber[] = "NewTestSerialNumber";
constexpr uint32_t kNewRegionSelection = 1;
constexpr uint32_t kNewSkuSelection = 1;
constexpr uint32_t kNewCustomLabelSelection = 2;
constexpr char kNewDramPartNum[] = "NewTestDramPartNum";

class UpdateDeviceInfoStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<UpdateDeviceInfoStateHandler> CreateStateHandler(
      const std::vector<bool>& wp_status,
      bool serial_number = true,
      bool region = true,
      bool sku = true,
      std::string custom_label = std::string(kCustomLabelTag),
      bool dram_part_num = true,
      bool region_list = true,
      bool sku_list = true,
      bool custom_label_list = true,
      bool set_serial_number = true,
      bool set_region = true,
      bool set_sku = true,
      bool set_custom_label = true,
      bool set_dram_part_num = true,
      bool sku_overflow = false,
      bool flush_out_vpd = true) {
    auto cbi_utils = std::make_unique<NiceMock<MockCbiUtils>>();
    auto cros_config_utils = std::make_unique<NiceMock<MockCrosConfigUtils>>();
    auto crossystem_utils = std::make_unique<StrictMock<MockCrosSystemUtils>>();
    auto regions_utils = std::make_unique<NiceMock<MockRegionsUtils>>();
    auto vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();

    {
      InSequence seq;
      for (bool enabled : wp_status) {
        EXPECT_CALL(*crossystem_utils,
                    GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
            .WillOnce(DoAll(SetArgPointee<1>(enabled ? 1 : 0), Return(true)));
      }
    }

    ON_CALL(*vpd_utils, FlushOutRoVpdCache())
        .WillByDefault(Return(flush_out_vpd));

    if (serial_number) {
      ON_CALL(*vpd_utils, GetSerialNumber(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSerialNumber), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetSerialNumber(_)).WillByDefault(Return(false));
    }

    if (region) {
      ON_CALL(*vpd_utils, GetRegion(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kRegion), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetRegion(_)).WillByDefault(Return(false));
    }

    if (sku && !sku_overflow) {
      ON_CALL(*cbi_utils, GetSkuId(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuId), Return(true)));
    } else if (sku_overflow) {
      ON_CALL(*cbi_utils, GetSkuId(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuIdOverflow), Return(true)));
    } else {
      ON_CALL(*cbi_utils, GetSkuId(_)).WillByDefault(Return(false));
    }

    if (!custom_label.empty()) {
      ON_CALL(*vpd_utils, GetCustomLabelTag(_, _))
          .WillByDefault(DoAll(SetArgPointee<0>(custom_label), Return(true)));
    } else {
      ON_CALL(*vpd_utils, GetCustomLabelTag(_, _)).WillByDefault(Return(false));
    }

    if (dram_part_num) {
      ON_CALL(*cbi_utils, GetDramPartNum(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kDramPartNum), Return(true)));
    } else {
      ON_CALL(*cbi_utils, GetDramPartNum(_)).WillByDefault(Return(false));
    }

    if (region_list) {
      ON_CALL(*regions_utils, GetRegionList(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kRegionList), Return(true)));
    } else {
      ON_CALL(*regions_utils, GetRegionList(_)).WillByDefault(Return(false));
    }

    ON_CALL(*cros_config_utils, GetRmadConfig(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(RmadConfig{.has_cbi = true}), Return(true)));
    if (sku_list) {
      ON_CALL(*cros_config_utils, GetSkuIdList(_))
          .WillByDefault(DoAll(SetArgPointee<0>(kSkuList), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetSkuIdList(_)).WillByDefault(Return(false));
    }

    if (custom_label_list) {
      ON_CALL(*cros_config_utils, GetCustomLabelTagList(_))
          .WillByDefault(
              DoAll(SetArgPointee<0>(kCustomLabelTagList), Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetCustomLabelTagList(_))
          .WillByDefault(Return(false));
    }

    if (set_serial_number) {
      ON_CALL(*vpd_utils, SetSerialNumber(_))
          .WillByDefault(DoAll(SaveArg<0>(&serial_number_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetSerialNumber(_)).WillByDefault(Return(false));
    }

    if (set_region) {
      ON_CALL(*vpd_utils, SetRegion(_))
          .WillByDefault(DoAll(SaveArg<0>(&region_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetRegion(_)).WillByDefault(Return(false));
    }

    if (set_sku) {
      ON_CALL(*cbi_utils, SetSkuId(_))
          .WillByDefault(DoAll(SaveArg<0>(&sku_set_), Return(true)));
    } else {
      ON_CALL(*cbi_utils, SetSkuId(_)).WillByDefault(Return(false));
    }

    if (set_custom_label) {
      ON_CALL(*vpd_utils, SetCustomLabelTag(_, _))
          .WillByDefault(DoAll(SaveArg<0>(&custom_label_set_), Return(true)));
    } else {
      ON_CALL(*vpd_utils, SetCustomLabelTag(_, _)).WillByDefault(Return(false));
    }

    if (set_dram_part_num) {
      ON_CALL(*cbi_utils, SetDramPartNum(_))
          .WillByDefault(DoAll(SaveArg<0>(&dram_part_num_set_), Return(true)));
    } else {
      ON_CALL(*cbi_utils, SetDramPartNum(_)).WillByDefault(Return(false));
    }

    return base::MakeRefCounted<UpdateDeviceInfoStateHandler>(
        json_store_, daemon_callback_, std::move(cbi_utils),
        std::move(cros_config_utils), std::move(crossystem_utils),
        std::move(regions_utils), std::move(vpd_utils));
  }

 protected:
  std::string serial_number_set_;
  std::string region_set_;
  uint64_t sku_set_;
  std::string custom_label_set_;
  std::string dram_part_num_set_;
};

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_Mlb_Success) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, true);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoMlb_Success) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_VarMissing_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  // No kMlbRepair set in |json_store_|.

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_WpEnabled_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({true});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_WP_ENABLED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoSerialNumberSuccess) {
  // wp_status, serial_number.
  auto handler = CreateStateHandler({false}, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoRegion_Success) {
  // wp_status, serial_number, region.
  auto handler = CreateStateHandler({false}, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoSku_Success) {
  // wp_status, serial_number, region, sku.
  auto handler = CreateStateHandler({false}, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoCustomLabel_Success) {
  // wp_status, serial_number, region, sku, custom_label.
  auto handler = CreateStateHandler({false}, true, true, true, "");
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoDramPartNum_Success) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num.
  auto handler =
      CreateStateHandler({false}, true, true, true, kCustomLabelTag, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoRegionList_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list.
  auto handler = CreateStateHandler({false}, true, true, true, kCustomLabelTag,
                                    true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, InitializeState_NoSkuList_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list.
  auto handler = CreateStateHandler({false}, true, true, true, kCustomLabelTag,
                                    true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_NoCustomLabelList_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list.
  auto handler = CreateStateHandler({false}, true, true, true, kCustomLabelTag,
                                    true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       InitializeState_CustomLabelListNotMatched_Success) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list.
  auto handler = CreateStateHandler({false}, true, true, true, "WLNotMatched",
                                    true, true, true, true);
  json_store_->SetValue(kMlbRepair, false);

  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  EXPECT_EQ(state.update_device_info().original_whitelabel_index(), -1);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_Success) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);

  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, kCustomLabelTagList[kNewCustomLabelSelection]);

  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_MissingState) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto [error, state_case] = handler->GetNextStateCase(RmadState());

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetSerialNumber_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number.
  auto handler =
      CreateStateHandler({false, false}, true, true, true, kCustomLabelTag,
                         true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetRegion_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region
  auto handler =
      CreateStateHandler({false, false}, true, true, true, kCustomLabelTag,
                         true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetRegion_WPEnabled_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region.
  auto handler =
      CreateStateHandler({false, true}, true, true, true, kCustomLabelTag, true,
                         true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_WP_ENABLED);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_CannotSetSku_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region,
  // set_sku
  auto handler =
      CreateStateHandler({false, false}, true, true, true, kCustomLabelTag,
                         true, true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetCustomLabel_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region,
  // set_sku, set_custom_label
  auto handler =
      CreateStateHandler({false, false}, true, true, true, kCustomLabelTag,
                         true, true, true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CannotSetDramPartNum_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region,
  // set_sku, set_custom_label, set_dram_part_num
  auto handler =
      CreateStateHandler({false, false}, true, true, true, kCustomLabelTag,
                         true, true, true, true, true, true, true, true, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, kCustomLabelTagList[kNewCustomLabelSelection]);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_RegionEmpty_Faled) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(-1);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_RegionWrongIndex_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kRegionList.size());
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuEmpty_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(-1);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_SkuWrongIndex_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kSkuList.size());
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CustomLabelEmpty_Success) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(-1);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, "");
  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_CustomLabelWrongIndex_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kCustomLabelTagList.size());
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlyMlb_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_mlb_repair(true);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySerialNumber_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_serial_number("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegion_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_region_index(
      kOriginalRegionSelection + 1);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlySku_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_sku_index(
      kOriginalSkuSelection + 1);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyCustomLabel_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_whitelabel_index(
      kOriginalCustomLabelSelection + 1);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_ReadOnlyDram_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_original_dram_part_number("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionListSize_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->add_region_list("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyRegionListItem_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_region_list(1, "");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySkuListSize_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->add_sku_list(0);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlySkuListItem_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_sku_list(1, 0);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyCustomLabelListSize_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->add_whitelabel_list("");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest,
       GetNextStateCase_ReadOnlyCustomLabelListItem_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  state.mutable_update_device_info()->set_whitelabel_list(1, "");

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_VIOLATION);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_SkuOverflow_Success) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region,
  // set_sku, set_custom_label, set_dram_part_num, sku_overflow.
  auto handler =
      CreateStateHandler({false}, true, true, true, kCustomLabelTag, true, true,
                         true, true, true, true, true, true, true, true);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  // We check if the original_sku is set to unmatched by trying
  // GetNextStateCase with |original_sku_index| = -1.
  state.mutable_update_device_info()->set_original_sku_index(-1);
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  EXPECT_EQ(serial_number_set_, kNewSerialNumber);
  EXPECT_EQ(region_set_, kRegionList[kNewRegionSelection]);
  EXPECT_EQ(sku_set_, kSkuList[kNewSkuSelection]);
  EXPECT_EQ(custom_label_set_, kCustomLabelTagList[kNewCustomLabelSelection]);
  EXPECT_EQ(dram_part_num_set_, kNewDramPartNum);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, GetNextStateCase_FlushOutVpd_Failed) {
  // wp_status, serial_number, region, sku, custom_label, dram_part_num,
  // region_list, sku_list, custom_label_list, set_serial_number, set_region,
  // set_sku, set_custom_label, set_dram_part_num, sku_overflow, flush_out_vpd.
  auto handler = CreateStateHandler({false, false}, true, true, true,
                                    kCustomLabelTag, true, true, true, true,
                                    true, true, true, true, true, false, false);
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto state = handler->GetState();
  state.mutable_update_device_info()->set_serial_number(kNewSerialNumber);
  state.mutable_update_device_info()->set_region_index(kNewRegionSelection);
  state.mutable_update_device_info()->set_sku_index(kNewSkuSelection);
  state.mutable_update_device_info()->set_whitelabel_index(
      kNewCustomLabelSelection);
  state.mutable_update_device_info()->set_dram_part_number(kNewDramPartNum);

  auto [error, state_case] = handler->GetNextStateCase(state);

  EXPECT_EQ(error, RMAD_ERROR_CANNOT_WRITE);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

TEST_F(UpdateDeviceInfoStateHandlerTest, TryGetNextStateCaseAtBoot_Failed) {
  // wp_status.
  auto handler = CreateStateHandler({false});
  json_store_->SetValue(kMlbRepair, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  auto [error, state_case] = handler->TryGetNextStateCaseAtBoot();
  EXPECT_EQ(error, RMAD_ERROR_TRANSITION_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kUpdateDeviceInfo);
}

}  // namespace rmad
