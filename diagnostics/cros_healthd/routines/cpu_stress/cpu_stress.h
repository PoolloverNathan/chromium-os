// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CPU_STRESS_CPU_STRESS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CPU_STRESS_CPU_STRESS_H_

#include <cstdint>
#include <memory>
#include <optional>

#include <base/time/time.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreateCpuStressRoutine(
    const std::optional<base::TimeDelta>& exec_duration);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_CPU_STRESS_CPU_STRESS_H_
