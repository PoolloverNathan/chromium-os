// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_DBUS_SERVICE_DBUS_SERVICE_DAEMON_H_
#define ML_DBUS_SERVICE_DBUS_SERVICE_DAEMON_H_

#include <memory>
#include <string>
#include <utility>

#include <base/strings/strcat.h>
#include <brillo/daemons/dbus_daemon.h>

namespace ml {

// Template class to easily create a DBusServiceDaemon.
// Parameters are a class that implements the DBus interface and the interface
// adaptor generated by the `generate_dbus_adaptors` rule.
template <class ServiceImpl, class Adaptor>
class DBusServiceDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit DBusServiceDaemon(const std::string& dbus_service_name)
      : brillo::DBusServiceDaemon(dbus_service_name),
        dbus_service_name_(dbus_service_name) {}
  DBusServiceDaemon(const DBusServiceDaemon&) = delete;
  DBusServiceDaemon& operator=(const DBusServiceDaemon&) = delete;

  ~DBusServiceDaemon() override = default;

 private:
  // brillo::DBusServiceDaemon:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    auto dbus_object = std::make_unique<brillo::dbus_utils::DBusObject>(
        /*object_manager=*/nullptr, bus_, Adaptor::GetObjectPath());

    dbus_service_ = std::make_unique<ServiceImpl>(std::move(dbus_object));

    dbus_service_->RegisterAsync(sequencer->GetHandler(
        /*descriptive_message=*/base::StrCat(
            {dbus_service_name_, ".RegisterAsync() failed."}),
        /*failure_is_fatal*/ true));
  }

  void OnShutdown(int* return_code) override {
    brillo::DBusServiceDaemon::OnShutdown(return_code);
    dbus_service_.reset();
  }

  const std::string dbus_service_name_;
  std::unique_ptr<ServiceImpl> dbus_service_;
};

}  // namespace ml

#endif  // ML_DBUS_SERVICE_DBUS_SERVICE_DAEMON_H_
