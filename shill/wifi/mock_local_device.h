// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_LOCAL_DEVICE_H_
#define SHILL_WIFI_MOCK_LOCAL_DEVICE_H_

#include "shill/wifi/local_device.h"

#include <gmock/gmock.h>

namespace shill {

class MockLocalDevice : public LocalDevice {
 public:
  MockLocalDevice(Manager* manager,
                  IfaceType type,
                  const std::string& link_name,
                  const std::string& mac_address,
                  uint32_t phy_index,
                  const EventCallback& callback)
      : LocalDevice(
            manager, type, link_name, mac_address, phy_index, callback) {}
  ~MockLocalDevice() override = default;

  bool Start() override { return true; }

  bool Stop() override { return true; }
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_LOCAL_DEVICE_H_
