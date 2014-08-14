// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_INPUT_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_INPUT_STUB_H_

#include <base/observer_list.h>

#include "power_manager/powerd/system/input_interface.h"

namespace power_manager {
namespace system {

// Stub implementation of InputInterface for use by tests.
class InputStub : public InputInterface {
 public:
  InputStub();
  virtual ~InputStub();

  bool wake_inputs_enabled() const { return wake_inputs_enabled_; }

  void set_lid_state(LidState state) { lid_state_ = state; }
  void set_usb_input_device_connected(bool connected) {
    usb_input_device_connected_ = connected;
  }
  void set_active_vt(int vt) { active_vt_ = vt; }

  // Notifies registered observers about various events.
  void NotifyObserversAboutLidState();
  void NotifyObserversAboutPowerButtonEvent(ButtonState state);

  // InputInterface implementation:
  void AddObserver(InputObserver* observer) override;
  void RemoveObserver(InputObserver* observer) override;
  LidState QueryLidState() override;
  bool IsUSBInputDeviceConnected() const override;
  int GetActiveVT() override;
  void SetInputDevicesCanWake(bool enable) override;

 private:
  // Current input state.
  LidState lid_state_;
  bool usb_input_device_connected_;
  int active_vt_;
  bool wake_inputs_enabled_;

  ObserverList<InputObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(InputStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_INPUT_STUB_H_
