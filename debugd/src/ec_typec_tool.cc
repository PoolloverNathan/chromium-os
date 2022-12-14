// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/ec_typec_tool.h"

#include <vector>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>

#include "debugd/src/ectool_util.h"

namespace {

constexpr char kSandboxDirPath[] = "/usr/share/policy/";
constexpr char kRunAs[] = "typecd_ec";

// Returns the ectool policy file corresponding to the provided
// |ectool_command|.
std::string GetEctoolPolicyFile(const std::string& ectool_command) {
  return base::StringPrintf("ectool_%s-seccomp.policy", ectool_command.c_str());
}

}  // namespace

namespace debugd {

std::string EcTypeCTool::GetInventory() {
  std::string output;
  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(GetEctoolPolicyFile("typec"));
  std::vector<std::string> ectool_args = {"inventory"};

  brillo::ErrorPtr error;
  if (!RunEctoolWithArgs(&error, seccomp_policy_path, ectool_args, kRunAs,
                         &output))
    output.clear();

  return output;
}

bool EcTypeCTool::EnterMode(brillo::ErrorPtr* error,
                            uint32_t port_num,
                            uint32_t mode,
                            std::string* output) {
  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(GetEctoolPolicyFile("typec"));

  std::vector<std::string> ectool_args = {"typeccontrol"};
  ectool_args.push_back(base::StringPrintf("%u", port_num));
  // 2nd argument is '2' for enter mode.
  ectool_args.push_back("2");
  ectool_args.push_back(base::StringPrintf("%u", mode));

  if (!RunEctoolWithArgs(error, seccomp_policy_path, ectool_args, kRunAs,
                         output))
    return false;

  return true;
}

bool EcTypeCTool::ExitMode(brillo::ErrorPtr* error,
                           uint32_t port_num,
                           std::string* output) {
  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(GetEctoolPolicyFile("typec"));

  std::vector<std::string> ectool_args = {"typeccontrol"};
  ectool_args.push_back(base::StringPrintf("%u", port_num));
  // 2nd argument is '0' for exit mode.
  ectool_args.push_back("0");

  if (!RunEctoolWithArgs(error, seccomp_policy_path, ectool_args, kRunAs,
                         output))
    return false;

  return true;
}

}  // namespace debugd
