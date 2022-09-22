// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_dhcp_config.h"

#include "shill/technology.h"

namespace shill {

MockDHCPConfig::MockDHCPConfig(ControlInterface* control_interface,
                               const std::string& device_name)
    : DHCPConfig(control_interface,
                 nullptr,
                 nullptr,
                 device_name,
                 std::string(),
                 std::string(),
                 Technology::kUnknown,
                 nullptr) {}

MockDHCPConfig::~MockDHCPConfig() = default;

void MockDHCPConfig::ProcessEventSignal(const std::string& reason,
                                        const KeyValueStore& configuration) {}
}  // namespace shill