// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_DBUS_BIOMETRICS_MANAGER_PROXY_BASE_H_
#define BIOD_DBUS_BIOMETRICS_MANAGER_PROXY_BASE_H_

#include <string>

#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_manager.h>

#include "biod/proto_bindings/constants.pb.h"

namespace biod {

const char* ScanResultToString(ScanResult result);

class BiometricsManagerProxyBase {
 public:
  using FinishCallback = base::Callback<void(bool success)>;

  BiometricsManagerProxyBase(const scoped_refptr<dbus::Bus>& bus,
                             const dbus::ObjectPath& path);

  const dbus::ObjectPath path() const;

  void SetFinishHandler(const FinishCallback& on_finish);

  dbus::ObjectProxy* StartAuthSession();

 protected:
  void OnFinish(bool success);

  void OnSignalConnected(const std::string& interface,
                         const std::string& signal,
                         bool success);

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_;

 private:
  friend class BiometricsManagerProxyBaseTest;

  void OnSessionFailed(dbus::Signal* signal);

  FinishCallback on_finish_;

  base::WeakPtrFactory<BiometricsManagerProxyBase> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(BiometricsManagerProxyBase);
};

}  // namespace biod

#endif  // BIOD_DBUS_BIOMETRICS_MANAGER_PROXY_BASE_H_
