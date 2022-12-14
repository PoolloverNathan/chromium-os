// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_utils.h"
#include "diagnostics/cros_healthd/routines/dns_resolution/dns_resolution.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::ash::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Values;
using ::testing::WithParamInterface;

// POD struct for DnsResolutionProblemTest.
struct DnsResolutionProblemTestParams {
  network_diagnostics_ipc::DnsResolutionProblem problem_enum;
  std::string failure_message;
};

class DnsResolutionRoutineTest : public testing::Test {
 protected:
  DnsResolutionRoutineTest() = default;
  DnsResolutionRoutineTest(const DnsResolutionRoutineTest&) = delete;
  DnsResolutionRoutineTest& operator=(const DnsResolutionRoutineTest&) = delete;

  void SetUp() override {
    routine_ = CreateDnsResolutionRoutine(network_diagnostics_adapter());
  }

  mojo_ipc::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnionPtr()};
    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return mojo_ipc::RoutineUpdate::New(update.progress_percent,
                                        std::move(update.output),
                                        std::move(update.routine_update_union));
  }

  MockNetworkDiagnosticsAdapter* network_diagnostics_adapter() {
    return mock_context_.network_diagnostics_adapter();
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
};

// Test that the DnsResolution routine can be run successfully.
TEST_F(DnsResolutionRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunDnsResolutionRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunDnsResolutionCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            network_diagnostics_ipc::RoutineProblems::NewDnsResolutionProblems(
                {}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kDnsResolutionRoutineNoProblemMessage);
}

// Test that the DnsResolution routine returns a kNotRun status when it is not
// run.
TEST_F(DnsResolutionRoutineTest, RoutineNotRun) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunDnsResolutionRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunDnsResolutionCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            network_diagnostics_ipc::RoutineProblems::NewDnsResolutionProblems(
                {}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kDnsResolutionRoutineNotRunMessage);
}

// Tests that the DnsResolution routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the DnsResolutionProblemTestParams POD struct):
// * |problem_enum| - The type of DnsResolution problem.
// * |failure_message| - Failure message for a problem.
class DnsResolutionProblemTest
    : public DnsResolutionRoutineTest,
      public WithParamInterface<DnsResolutionProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  DnsResolutionProblemTestParams params() const { return GetParam(); }
};

// Test that the DnsResolution routine handles the given DNS resolution problem.
TEST_P(DnsResolutionProblemTest, HandleDnsResolutionProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunDnsResolutionRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               RunDnsResolutionCallback callback) {
        auto result = CreateResult(
            network_diagnostics_ipc::RoutineVerdict::kProblem,
            network_diagnostics_ipc::RoutineProblems::NewDnsResolutionProblems(
                {params().problem_enum}));
        std::move(callback).Run(std::move(result));
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    DnsResolutionProblemTest,
    Values(DnsResolutionProblemTestParams{
        network_diagnostics_ipc::DnsResolutionProblem::kFailedToResolveHost,
        kDnsResolutionRoutineFailedToResolveHostProblemMessage}));

}  // namespace
}  // namespace diagnostics
