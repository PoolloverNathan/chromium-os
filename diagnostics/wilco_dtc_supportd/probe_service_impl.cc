// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/probe_service_impl.h"

#include <iterator>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

ProbeServiceImpl::ProbeServiceImpl(Delegate* delegate) : delegate_(delegate) {
  DCHECK(delegate_);
}

ProbeServiceImpl::~ProbeServiceImpl() {
  RunInFlightCallbacks();
}

void ProbeServiceImpl::ProbeTelemetryInfo(
    std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories,
    ProbeTelemetryInfoCallback callback) {
  if (!BindCrosHealthdProbeServiceIfNeeded()) {
    LOG(WARNING) << "ProbeTelemetryInfo called before mojo was bootstrapped.";
    std::move(callback).Run(nullptr);
    return;
  }

  const size_t callback_key = next_callback_key_;
  next_callback_key_++;
  DCHECK_EQ(callbacks_.count(callback_key), 0);
  callbacks_.insert({callback_key, std::move(callback)});
  service_ptr_->ProbeTelemetryInfo(
      std::move(categories),
      base::Bind(&ProbeServiceImpl::ForwardProbeTelemetryInfoResponse,
                 weak_ptr_factory_.GetWeakPtr(), callback_key));
}

void ProbeServiceImpl::ForwardProbeTelemetryInfoResponse(
    size_t callback_key,
    chromeos::cros_healthd::mojom::TelemetryInfoPtr telemetry_info) {
  auto it = callbacks_.find(callback_key);
  if (it == callbacks_.end()) {
    LOG(ERROR) << "Unknown callback_key for received mojo ProbeTelemetryInfo "
                  "response: "
               << callback_key;
    return;
  }

  std::move(it->second).Run(std::move(telemetry_info));
  callbacks_.erase(it);
}

bool ProbeServiceImpl::BindCrosHealthdProbeServiceIfNeeded() {
  if (service_ptr_.is_bound())
    return true;

  auto request = mojo::MakeRequest(&service_ptr_);

  service_ptr_.set_connection_error_handler(base::Bind(
      &ProbeServiceImpl::OnDisconnect, weak_ptr_factory_.GetWeakPtr()));

  return delegate_->BindCrosHealthdProbeService(std::move(request));
}

void ProbeServiceImpl::OnDisconnect() {
  VLOG(1) << "Mojo connection to cros_healthd probe service is closed.";
  RunInFlightCallbacks();
  service_ptr_.reset();
}

void ProbeServiceImpl::RunInFlightCallbacks() {
  for (auto& it : callbacks_) {
    std::move(it.second).Run(nullptr);
  }
  callbacks_.clear();
}

}  // namespace diagnostics
