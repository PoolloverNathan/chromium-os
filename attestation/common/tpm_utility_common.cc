// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/tpm_utility_common.h"

#include <memory>
#include <vector>

#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/bind_post_task.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

namespace attestation {

TpmUtilityCommon::TpmUtilityCommon()
    : tpm_manager_utility_(tpm_manager::TpmManagerUtility::GetSingleton()) {}

TpmUtilityCommon::TpmUtilityCommon(
    tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility) {}

TpmUtilityCommon::~TpmUtilityCommon() {}

bool TpmUtilityCommon::Initialize() {
  BuildValidPCR0Values();
  if (!tpm_manager_utility_) {
    LOG(INFO) << __func__ << "Reinitialize tpm_manager utility";
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
  }
  tpm_manager_utility_->AddOwnershipCallback(base::BindPostTask(
      base::SequencedTaskRunnerHandle::Get(),
      base::BindRepeating(&TpmUtilityCommon::OnOwnershipTakenSignal,
                          base::Unretained(this))));
  return tpm_manager_utility_;
}

void TpmUtilityCommon::UpdateTpmLocalData(
    const tpm_manager::LocalData& local_data) {
  endorsement_password_ = local_data.endorsement_password();
  owner_password_ = local_data.owner_password();
  delegate_blob_ = local_data.owner_delegate().blob();
  delegate_secret_ = local_data.owner_delegate().secret();
}

void TpmUtilityCommon::OnOwnershipTakenSignal() {
  if (is_ready_) {
    return;
  }
  CHECK(tpm_manager_utility_);
  tpm_manager::LocalData local_data;
  if (!tpm_manager_utility_->GetOwnershipTakenSignalStatus(nullptr, nullptr,
                                                           &local_data)) {
    LOG(ERROR) << __func__ << ": Failed to get local data.";
    return;
  }
  is_ready_ = true;
  UpdateTpmLocalData(local_data);
}

bool TpmUtilityCommon::IsTpmReady() {
  if (has_cache_tpm_state_) {
    return is_ready_;
  }
  has_cache_tpm_state_ = true;

  if (is_ready_) {
    return true;
  }
  tpm_manager::LocalData local_data;
  bool is_enabled{false};
  bool is_owned{false};
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get tpm_manager utility.";
      return false;
    }
  }
  if (!tpm_manager_utility_->GetTpmStatus(&is_enabled, &is_owned,
                                          &local_data)) {
    LOG(ERROR) << __func__ << ": Failed to get tpm status from tpm_manager.";
    return false;
  }
  is_ready_ = is_enabled && is_owned;
  UpdateTpmLocalData(local_data);
  return is_ready_;
}

void TpmUtilityCommon::BuildValidPCR0Values() {
  // 3-byte boot mode:
  //  - byte 0: 1 if in developer mode, 0 otherwise,
  //  - byte 1: 1 if in recovery mode, 0 otherwise,
  //  - byte 2: 1 if verified firmware, 0 if developer firmware.
  constexpr char kKnownBootModes[][3] = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0},
                                         {0, 1, 1}, {1, 0, 0}, {1, 0, 1},
                                         {1, 1, 0}, {1, 1, 1}};

  for (const auto& mode_array : kKnownBootModes) {
    const std::string mode(std::begin(mode_array), std::end(mode_array));
    valid_pcr0_values_.insert(GetPCRValueForMode(mode));
  }
}

bool TpmUtilityCommon::IsPCR0Valid() {
  std::string pcr0_value;
  if (!ReadPCR(0, &pcr0_value)) {
    LOG(ERROR) << __func__ << "Failed to read PCR0";
    return false;
  }

  if (!base::Contains(valid_pcr0_values_, pcr0_value)) {
    LOG(ERROR) << "Encountered invalid PCR0 value: "
               << base::HexEncode(pcr0_value.data(), pcr0_value.size());
    return false;
  }

  return true;
}

bool TpmUtilityCommon::GetEndorsementPassword(std::string* password) {
  if (endorsement_password_.empty()) {
    if (!IsTpmReady()) {
      return false;
    }
    if (endorsement_password_.empty()) {
      LOG(WARNING) << ": TPM endorsement password is not available.";
      return false;
    }
  }
  *password = endorsement_password_;
  return true;
}

bool TpmUtilityCommon::GetOwnerPassword(std::string* password) {
  if (owner_password_.empty()) {
    if (!IsTpmReady()) {
      return false;
    }
    if (owner_password_.empty()) {
      LOG(WARNING) << ": TPM owner password is not available.";
      return false;
    }
  }
  *password = owner_password_;
  return true;
}

bool TpmUtilityCommon::RemoveOwnerDependency() {
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get tpm_manager utility.";
      return false;
    }
  }
  return tpm_manager_utility_->RemoveOwnerDependency(
      tpm_manager::kTpmOwnerDependency_Attestation);
}

}  // namespace attestation
