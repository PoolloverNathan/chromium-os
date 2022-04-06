// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgbkbd_daemon.h"

#include <memory>
#include <utility>

#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/rgbkbd/dbus-constants.h>

namespace rgbkbd {
DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::RgbkbdAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kRgbkbdServicePath)) {}

void DBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

uint32_t DBusAdaptor::GetRgbKeyboardCapabilities() {
  // TODO(michaelcheco): Shutdown DBus service if the keyboard is not
  // supported.
  return rgb_keyboard_controller_.GetRgbKeyboardCapabilities();
}

RgbkbdDaemon::RgbkbdDaemon() : DBusServiceDaemon(kRgbkbdServicePath) {}

void RgbkbdDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_.reset(new DBusAdaptor(bus_));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace rgbkbd