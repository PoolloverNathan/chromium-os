// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSTEM_API_DBUS_PCIGUARD_DBUS_CONSTANTS_H_
#define SYSTEM_API_DBUS_PCIGUARD_DBUS_CONSTANTS_H_

namespace pciguard {

constexpr char kPciguardServiceName[] = "org.chromium.pciguard";
constexpr char kPciguardServiceInterface[] = "org.chromium.pciguard";
constexpr char kPciguardServicePath[] = "/org/chromium/pciguard";
constexpr char kSetExternalPciDevicesPermissionMethod[] =
    "SetExternalPciDevicesPermission";

}  // namespace pciguard

#endif  // SYSTEM_API_DBUS_PCIGUARD_DBUS_CONSTANTS_H_
