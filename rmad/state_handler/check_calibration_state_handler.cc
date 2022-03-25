// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/check_calibration_state_handler.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/logging.h>

#include "rmad/constants.h"
#include "rmad/utils/calibration_utils.h"
#include "rmad/utils/fake_iio_sensor_probe_utils.h"
#include "rmad/utils/iio_sensor_probe_utils_impl.h"

namespace rmad {

namespace {

// Convert a dictionary of {CalibrationSetupInstruction:
// {RmadComponent: CalibrationComponentStatus}} to a |RmadState|.
RmadState ConvertDictionaryToState(
    const InstructionCalibrationStatusMap& calibration_map) {
  auto check_calibration = std::make_unique<CheckCalibrationState>();
  for (const auto& [unused_instruction, components] : calibration_map) {
    for (const auto& [component, status] : components) {
      if (!IsValidCalibrationComponent(component)) {
        LOG(WARNING) << "Dictionary contains unsupported component "
                     << RmadComponent_Name(component);
        continue;
      }
      CalibrationComponentStatus* calibration_component_status =
          check_calibration->add_components();
      calibration_component_status->set_component(component);
      calibration_component_status->set_status(status);
      double progress = 0.0;
      if (status == CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE) {
        progress = 1.0;
      } else if (status ==
                 CalibrationComponentStatus::RMAD_CALIBRATION_FAILED) {
        progress = -1.0;
      }
      calibration_component_status->set_progress(progress);
    }
  }

  RmadState state;
  state.set_allocated_check_calibration(check_calibration.release());
  return state;
}

}  // namespace

namespace fake {

FakeCheckCalibrationStateHandler::FakeCheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : CheckCalibrationStateHandler(
          json_store, std::make_unique<FakeIioSensorProbeUtils>()) {}

}  // namespace fake

CheckCalibrationStateHandler::CheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store)
    : BaseStateHandler(json_store) {
  iio_sensor_probe_utils_ = std::make_unique<IioSensorProbeUtilsImpl>();
}

CheckCalibrationStateHandler::CheckCalibrationStateHandler(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<IioSensorProbeUtils> iio_sensor_probe_utils)
    : BaseStateHandler(json_store),
      iio_sensor_probe_utils_(std::move(iio_sensor_probe_utils)) {}

RmadErrorCode CheckCalibrationStateHandler::InitializeState() {
  // Always probe again and use the probe results to update |state_|.
  std::set<RmadComponent> probed_components = iio_sensor_probe_utils_->Probe();
  // Update probeable components using runtime_probe results.
  for (RmadComponent component : probed_components) {
    // Ignore the components that cannot be calibrated.
    if (std::find(kComponentsNeedManualCalibration.begin(),
                  kComponentsNeedManualCalibration.end(),
                  component) == kComponentsNeedManualCalibration.end()) {
      continue;
    }
    calibration_map_[GetCalibrationSetupInstruction(component)][component] =
        CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
  }

  // Ignore the return value, since we can initialize state handler from an
  // empty or fulfilled dictionary.
  InstructionCalibrationStatusMap original_calibration_map;
  GetCalibrationMap(json_store_, &original_calibration_map);

  // We mark all components with an unexpected status as failed because it may
  // have some errors.
  for (auto [instruction, components] : original_calibration_map) {
    for (auto [component, status] : components) {
      if (calibration_map_.count(instruction) &&
          calibration_map_[instruction].count(component)) {
        if (IsInProgressStatus(status) || IsUnknownStatus(status)) {
          status = CalibrationComponentStatus::RMAD_CALIBRATION_FAILED;
        }
        calibration_map_[instruction][component] = status;
      }
    }
  }

  if (!SetCalibrationMap(json_store_, calibration_map_)) {
    LOG(ERROR) << "Failed to set calibration status.";
    return RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED;
  }

  state_ = ConvertDictionaryToState(calibration_map_);
  return RMAD_ERROR_OK;
}

BaseStateHandler::GetNextStateCaseReply
CheckCalibrationStateHandler::GetNextStateCase(const RmadState& state) {
  bool need_calibration;
  RmadErrorCode error_code;
  if (!CheckIsCalibrationRequired(state, &need_calibration, &error_code)) {
    return NextStateCaseWrapper(error_code);
  }

  // kWipeDevice should be set by previous states.
  bool wipe_device;
  if (!json_store_->GetValue(kWipeDevice, &wipe_device)) {
    LOG(ERROR) << "Variable " << kWipeDevice << " not found";
    return NextStateCaseWrapper(RMAD_ERROR_TRANSITION_FAILED);
  }

  state_ = state;
  SetCalibrationMap(json_store_, calibration_map_);

  if (need_calibration) {
    return NextStateCaseWrapper(RmadState::StateCase::kSetupCalibration);
  }

  if (wipe_device) {
    return NextStateCaseWrapper(RmadState::StateCase::kFinalize);
  } else {
    return NextStateCaseWrapper(RmadState::StateCase::kWpEnablePhysical);
  }
}

bool CheckCalibrationStateHandler::CheckIsUserSelectionValid(
    const CheckCalibrationState& user_selection, RmadErrorCode* error_code) {
  CHECK(state_.has_check_calibration());
  // Here we make sure that the size is the same, and then we can only check
  // whether the components from user selection are all in the dictionary.
  if (user_selection.components_size() !=
      state_.check_calibration().components_size()) {
    LOG(ERROR) << "Size of components has been changed!";
    *error_code = RMAD_ERROR_CALIBRATION_COMPONENT_MISSING;
    return false;
  }

  // If a calibratable component is probed, it should be in the dictionary.
  // Otherwise, the component from user selection is invalid.
  for (int i = 0; i < user_selection.components_size(); ++i) {
    const auto& component = user_selection.components(i).component();
    const auto& status = user_selection.components(i).status();
    const auto instruction = GetCalibrationSetupInstruction(component);
    if (!calibration_map_[instruction].count(component)) {
      LOG(ERROR) << RmadComponent_Name(component)
                 << " has not been probed, it should not be selected!";
      *error_code = RMAD_ERROR_CALIBRATION_COMPONENT_INVALID;
      return false;
    } else if (!IsWaitingForCalibration(status) && !IsCompleteStatus(status)) {
      LOG(ERROR) << RmadComponent_Name(component) << "'s status is "
                 << CalibrationComponentStatus::CalibrationStatus_Name(status)
                 << ", but it should be chosen to skip or waiting to retry.";
      *error_code = RMAD_ERROR_CALIBRATION_STATUS_MISSING;
      return false;
    }
  }

  return true;
}

bool CheckCalibrationStateHandler::CheckIsCalibrationRequired(
    const RmadState& state, bool* need_calibration, RmadErrorCode* error_code) {
  if (!state.has_check_calibration()) {
    LOG(ERROR) << "RmadState missing |check calibration| state.";
    *error_code = RMAD_ERROR_REQUEST_INVALID;
    return false;
  }

  *need_calibration = false;

  const CheckCalibrationState& user_selection = state.check_calibration();
  if (!CheckIsUserSelectionValid(user_selection, error_code)) {
    return false;
  }

  // All components here are valid.
  for (int i = 0; i < user_selection.components_size(); ++i) {
    const CalibrationComponentStatus& component_status =
        user_selection.components(i);
    RmadComponent component = component_status.component();
    CalibrationComponentStatus::CalibrationStatus status =
        component_status.status();

    switch (status) {
      case CalibrationComponentStatus::RMAD_CALIBRATION_WAITING:
      case CalibrationComponentStatus::RMAD_CALIBRATION_FAILED:
        *need_calibration = true;
        break;
      // For those already calibrated and skipped components, we don't need to
      // calibrate them.
      case CalibrationComponentStatus::RMAD_CALIBRATION_COMPLETE:
      case CalibrationComponentStatus::RMAD_CALIBRATION_SKIP:
        break;
      case CalibrationComponentStatus::RMAD_CALIBRATION_UNKNOWN:
      default:
        NOTREACHED();
        *error_code = RMAD_ERROR_REQUEST_ARGS_MISSING;
        LOG(ERROR)
            << "RmadState component missing |calibration_status| argument.";
        return false;
    }

    // For sensors that failed to calibrate, we need to retry, so we set them to
    // wait for calibration.
    if (status == CalibrationComponentStatus::RMAD_CALIBRATION_FAILED) {
      status = CalibrationComponentStatus::RMAD_CALIBRATION_WAITING;
    }
    calibration_map_[GetCalibrationSetupInstruction(component)][component] =
        status;
  }

  *error_code = RMAD_ERROR_OK;
  return true;
}

}  // namespace rmad
