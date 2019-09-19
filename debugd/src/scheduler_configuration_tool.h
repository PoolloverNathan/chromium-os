// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_SCHEDULER_CONFIGURATION_TOOL_H_
#define DEBUGD_SRC_SCHEDULER_CONFIGURATION_TOOL_H_

#include <string>

#include <base/macros.h>
#include <brillo/errors/error.h>

namespace debugd {

class SchedulerConfigurationTool {
 public:
  SchedulerConfigurationTool() = default;
  ~SchedulerConfigurationTool() = default;

  // This sets the core sharing policy.
  bool SetPolicy(const std::string& policy,
                 bool lock_policy,
                 brillo::ErrorPtr* error,
                 uint32_t* num_cores_disabled);

 private:
  // Set to true when |SetAndLockConservativePolicy| is called for the first
  // time. If set to |true| can't be set to false and configuration policy can't
  // be changed.
  bool policy_locked_conservative_ = false;

  DISALLOW_COPY_AND_ASSIGN(SchedulerConfigurationTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_SCHEDULER_CONFIGURATION_TOOL_H_
