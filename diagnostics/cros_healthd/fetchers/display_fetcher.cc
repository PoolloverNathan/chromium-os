// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;

void FillDisplaySize(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                     const uint32_t connector_id,
                     mojo_ipc::NullableUint32Ptr* out_width,
                     mojo_ipc::NullableUint32Ptr* out_height) {
  uint32_t width;
  uint32_t height;
  if (!libdrm_util->FillDisplaySize(connector_id, &width, &height))
    return;

  *out_width = mojo_ipc::NullableUint32::New(width);
  *out_height = mojo_ipc::NullableUint32::New(height);
}

void FillDisplayResolution(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                           const uint32_t connector_id,
                           mojo_ipc::NullableUint32Ptr* out_horizontal,
                           mojo_ipc::NullableUint32Ptr* out_vertical) {
  uint32_t horizontal;
  uint32_t vertical;
  if (!libdrm_util->FillDisplayResolution(connector_id, &horizontal, &vertical))
    return;

  *out_horizontal = mojo_ipc::NullableUint32::New(horizontal);
  *out_vertical = mojo_ipc::NullableUint32::New(vertical);
}

void FillDisplayRefreshRate(const std::unique_ptr<LibdrmUtil>& libdrm_util,
                            const uint32_t connector_id,
                            mojo_ipc::NullableDoublePtr* out_refresh_rate) {
  double refresh_rate;
  if (!libdrm_util->FillDisplayRefreshRate(connector_id, &refresh_rate))
    return;

  *out_refresh_rate = mojo_ipc::NullableDouble::New(refresh_rate);
}

mojo_ipc::EmbeddedDisplayInfoPtr FetchEmbeddedDisplayInfo(
    const std::unique_ptr<LibdrmUtil>& libdrm_util) {
  auto info = mojo_ipc::EmbeddedDisplayInfo::New();
  auto connector_id = libdrm_util->GetEmbeddedDisplayConnectorID();
  libdrm_util->FillPrivacyScreenInfo(connector_id,
                                     &info->privacy_screen_supported,
                                     &info->privacy_screen_enabled);

  FillDisplaySize(libdrm_util, connector_id, &info->display_width,
                  &info->display_height);
  FillDisplayResolution(libdrm_util, connector_id, &info->resolution_horizontal,
                        &info->resolution_vertical);
  FillDisplayRefreshRate(libdrm_util, connector_id, &info->refresh_rate);

  EdidInfo edid_info;
  if (libdrm_util->FillEdidInfo(connector_id, &edid_info)) {
    info->manufacturer = edid_info.manufacturer;
    info->model_id = mojo_ipc::NullableUint16::New(edid_info.model_id);
    if (edid_info.serial_number.has_value())
      info->serial_number =
          mojo_ipc::NullableUint32::New(edid_info.serial_number.value());
    if (edid_info.manufacture_week.has_value())
      info->manufacture_week =
          mojo_ipc::NullableUint8::New(edid_info.manufacture_week.value());
    if (edid_info.manufacture_year.has_value())
      info->manufacture_year =
          mojo_ipc::NullableUint16::New(edid_info.manufacture_year.value());
    info->edid_version = edid_info.edid_version;
    if (edid_info.is_degital_input)
      info->input_type = mojo_ipc::DisplayInputType::kDigital;
    else
      info->input_type = mojo_ipc::DisplayInputType::kAnalog;
    info->display_name = edid_info.display_name;
  }
  return info;
}

std::optional<std::vector<mojo_ipc::ExternalDisplayInfoPtr>>
FetchExternalDisplayInfo(const std::unique_ptr<LibdrmUtil>& libdrm_util) {
  auto connector_ids = libdrm_util->GetExternalDisplayConnectorID();
  if (connector_ids.size() == 0)
    return std::nullopt;

  std::vector<mojo_ipc::ExternalDisplayInfoPtr> infos;
  for (const auto& connector_id : connector_ids) {
    auto info = mojo_ipc::ExternalDisplayInfo::New();

    FillDisplaySize(libdrm_util, connector_id, &info->display_width,
                    &info->display_height);
    FillDisplayResolution(libdrm_util, connector_id,
                          &info->resolution_horizontal,
                          &info->resolution_vertical);
    FillDisplayRefreshRate(libdrm_util, connector_id, &info->refresh_rate);

    EdidInfo edid_info;
    if (libdrm_util->FillEdidInfo(connector_id, &edid_info)) {
      info->manufacturer = edid_info.manufacturer;
      info->model_id = mojo_ipc::NullableUint16::New(edid_info.model_id);
      if (edid_info.serial_number.has_value())
        info->serial_number =
            mojo_ipc::NullableUint32::New(edid_info.serial_number.value());
      if (edid_info.manufacture_week.has_value())
        info->manufacture_week =
            mojo_ipc::NullableUint8::New(edid_info.manufacture_week.value());
      if (edid_info.manufacture_year.has_value())
        info->manufacture_year =
            mojo_ipc::NullableUint16::New(edid_info.manufacture_year.value());
      info->edid_version = edid_info.edid_version;
      if (edid_info.is_degital_input)
        info->input_type = mojo_ipc::DisplayInputType::kDigital;
      else
        info->input_type = mojo_ipc::DisplayInputType::kAnalog;
      info->display_name = edid_info.display_name;
    }
    infos.push_back(std::move(info));
  }

  return infos;
}

}  // namespace

void DisplayFetcher::FetchDisplayInfo(
    DisplayFetcher::FetchDisplayInfoCallback&& callback) {
  auto libdrm_util = context_->CreateLibdrmUtil();
  if (!libdrm_util->Initialize()) {
    std::move(callback).Run(mojo_ipc::DisplayResult::NewError(
        CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                               "Failed to initialize libdrm_util object.")));
    return;
  }

  auto display_info = mojo_ipc::DisplayInfo::New();
  auto edp_info = FetchEmbeddedDisplayInfo(libdrm_util);
  display_info->edp_info = std::move(edp_info);

  auto dp_infos = FetchExternalDisplayInfo(libdrm_util);
  if (dp_infos) {
    display_info->dp_infos = std::move(dp_infos);
  }

  std::move(callback).Run(
      mojo_ipc::DisplayResult::NewDisplayInfo(std::move(display_info)));
}

}  // namespace diagnostics
