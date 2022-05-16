// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/arc_vm.h"

#include <string>

#include <base/containers/contains.h>
#include <base/strings/stringprintf.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>

namespace vm_tools {
namespace concierge {
namespace {
constexpr int kSeneschalServerPort = 3000;
}  // namespace

TEST(ArcVmTest, NonDevModeKernelParams) {
  crossystem::fake::CrossystemFake cros_system;
  cros_system.VbSetSystemPropertyInt("cros_debug", 0);
  std::vector<std::string> params =
      ArcVm::GetKernelParams(&cros_system, kSeneschalServerPort);
  EXPECT_TRUE(base::Contains(params, "androidboot.dev_mode=0"));
  EXPECT_TRUE(base::Contains(params, "androidboot.disable_runas=1"));
}

TEST(ArcVmTest, DevModeKernelParams) {
  crossystem::fake::CrossystemFake cros_system;
  cros_system.VbSetSystemPropertyInt("cros_debug", 1);
  std::vector<std::string> params =
      ArcVm::GetKernelParams(&cros_system, kSeneschalServerPort);
  EXPECT_TRUE(base::Contains(params, "androidboot.dev_mode=1"));
  EXPECT_TRUE(base::Contains(params, "androidboot.disable_runas=0"));
}

TEST(ArcVmTest, SeneschalServerPortParam) {
  crossystem::fake::CrossystemFake cros_system;
  std::vector<std::string> params =
      ArcVm::GetKernelParams(&cros_system, kSeneschalServerPort);
  EXPECT_TRUE(base::Contains(
      params, base::StringPrintf("androidboot.seneschal_server_port=%d",
                                 kSeneschalServerPort)));
}

TEST(ArcVmTest, ChromeOsChannelStable) {
  base::test::ScopedChromeOSVersionInfo info(
      "CHROMEOS_RELEASE_TRACK=stable-channel", base::Time::Now());
  crossystem::fake::CrossystemFake cros_system;
  EXPECT_TRUE(
      base::Contains(ArcVm::GetKernelParams(&cros_system, kSeneschalServerPort),
                     "androidboot.chromeos_channel=stable"));
}

TEST(ArcVmTest, ChromeOsChannelTestImage) {
  base::test::ScopedChromeOSVersionInfo info(
      "CHROMEOS_RELEASE_TRACK=testimage-channel", base::Time::Now());
  crossystem::fake::CrossystemFake cros_system;
  EXPECT_TRUE(
      base::Contains(ArcVm::GetKernelParams(&cros_system, kSeneschalServerPort),
                     "androidboot.vshd_service_override=vshd_for_test"));
}

TEST(ArcVmTest, ChromeOsChannelUnknown) {
  base::test::ScopedChromeOSVersionInfo info("CHROMEOS_RELEASE_TRACK=invalid",
                                             base::Time::Now());
  crossystem::fake::CrossystemFake cros_system;
  EXPECT_TRUE(
      base::Contains(ArcVm::GetKernelParams(&cros_system, kSeneschalServerPort),
                     "androidboot.chromeos_channel=unknown"));
}

}  // namespace concierge
}  // namespace vm_tools