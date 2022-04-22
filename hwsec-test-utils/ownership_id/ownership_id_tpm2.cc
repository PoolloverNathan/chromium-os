// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <crypto/sha2.h>
#include <tpm_manager/client/tpm_manager_utility.h>

#include "hwsec-test-utils/ownership_id/ownership_id_tpm2.h"

namespace {
// A constant to represent the corner case.
constexpr char kNoLockoutPassword[] = "NO_LOCKOUT_PASSWORD";
}  // namespace

namespace hwsec_test_utils {

bool OwnershipIdTpm2::InitializeTpmManagerUtility() {
  if (!tpm_manager_utility_) {
    auto tpm_manager_utility =
        std::make_unique<tpm_manager::TpmManagerUtility>();
    if (!tpm_manager_utility->Initialize()) {
      LOG(ERROR) << "Failed to init TpmManagerUtility.";
      return false;
    }
    tpm_manager_utility_ = std::move(tpm_manager_utility);
  }
  return true;
}

std::optional<std::string> OwnershipIdTpm2::Get() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << "InitializeTpmManagerUtility failed.";
    return std::nullopt;
  }

  bool is_enabled;
  bool is_owned;
  tpm_manager::LocalData local_data;
  if (!tpm_manager_utility_->GetTpmStatus(&is_enabled, &is_owned,
                                          &local_data)) {
    LOG(ERROR) << "GetTpmStatus failed.";
    return std::nullopt;
  }

  if (!is_enabled) {
    LOG(ERROR) << "TPM is not enabled.";
    return std::nullopt;
  }

  if (!is_owned) {
    // Return empty string for unowned status.
    return "";
  }

  if (local_data.lockout_password().empty()) {
    LOG(WARNING) << "Empty lockout password.";
    return kNoLockoutPassword;
  }

  const std::string id =
      crypto::SHA256HashString(local_data.lockout_password());
  return base::HexEncode(id.data(), id.length());
}

}  // namespace hwsec_test_utils
