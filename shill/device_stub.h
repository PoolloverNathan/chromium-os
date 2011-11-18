// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_STUB_DEVICE_
#define SHILL_STUB_DEVICE_

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_ptr.h>

#include <vector>

#include "shill/device.h"
#include "shill/event_dispatcher.h"
#include "shill/service.h"

namespace shill {

class ControlInterface;
class DeviceAdaptorInterface;
class EventDispatcher;
class Endpoint;
class DeviceInfo;
class Manager;

// Non-functional Device subclass used for non-operable or blacklisted devices
class DeviceStub : public Device {
 public:
  DeviceStub(ControlInterface *control_interface,
             EventDispatcher *dispatcher,
             Manager *manager,
             const std::string &link_name,
             const std::string &address,
             int interface_index,
             Technology::Identifier technology)
      : Device(control_interface, dispatcher, manager, link_name, address,
               interface_index, technology) {}
  void Start() {}
  void Stop() {}
  bool TechnologyIs(const Technology::Identifier type) const {
    return type == technology();
  }

 private:

  DISALLOW_COPY_AND_ASSIGN(DeviceStub);
};

}  // namespace shill

#endif  // SHILL_STUB_DEVICE_
