// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/urandom/urandom.h"

#include <optional>
#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

// Path to the urandom executable.
constexpr char kUrandomExePath[] = "/usr/libexec/diagnostics/urandom";

}  // namespace

const base::TimeDelta kUrandomDefaultLength = base::Seconds(10);

std::unique_ptr<DiagnosticRoutine> CreateUrandomRoutine(
    const std::optional<base::TimeDelta>& length_seconds) {
  base::TimeDelta routine_duration =
      length_seconds.value_or(kUrandomDefaultLength);
  return std::make_unique<SubprocRoutine>(
      base::CommandLine(std::vector<std::string>{
          kUrandomExePath,
          "--time_delta_ms=" +
              std::to_string(routine_duration.InMilliseconds()),
          "--urandom_path=/dev/urandom"}),
      routine_duration);
}

}  // namespace diagnostics
