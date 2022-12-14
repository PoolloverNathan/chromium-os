// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/json/json_writer.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <base/values.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/system/handle.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/routines/memory/memory.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

// Location of files containing test data (fake memtester output).
constexpr char kTestDataRoot[] = "cros_healthd/routines/memory/testdata";

// Constructs expected output for the memory routine.
std::string ConstructOutput() {
  base::Value subtest_dict(base::Value::Type::DICTIONARY);
  subtest_dict.SetStringKey("bitFlip", "ok");
  subtest_dict.SetStringKey("bitSpread", "ok");
  subtest_dict.SetStringKey("blockSequential", "ok");
  subtest_dict.SetStringKey("checkerboard", "ok");
  subtest_dict.SetStringKey("compareAND", "ok");
  subtest_dict.SetStringKey("compareDIV", "ok");
  subtest_dict.SetStringKey("compareMUL", "ok");
  subtest_dict.SetStringKey("compareOR", "ok");
  subtest_dict.SetStringKey("compareSUB", "ok");
  subtest_dict.SetStringKey("compareXOR", "ok");
  subtest_dict.SetStringKey("randomValue", "ok");
  subtest_dict.SetStringKey("sequentialIncrement", "ok");
  subtest_dict.SetStringKey("solidBits", "ok");
  subtest_dict.SetStringKey("stuckAddress", "ok");
  subtest_dict.SetStringKey("walkingOnes", "ok");
  subtest_dict.SetStringKey("walkingZeroes", "ok");

  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetKey("subtests", std::move(subtest_dict));
  result_dict.SetStringKey("bytesTested", "104857600");
  result_dict.SetStringKey("memtesterVersion", "4.2.2 (64-bit)");

  base::Value output_dict(base::Value::Type::DICTIONARY);
  output_dict.SetKey("resultDetails", std::move(result_dict));

  std::string json;
  base::JSONWriter::WriteWithOptions(
      output_dict, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

class MemoryRoutineTest : public BaseFileTest {
 protected:
  MemoryRoutineTest() = default;
  MemoryRoutineTest(const MemoryRoutineTest&) = delete;
  MemoryRoutineTest& operator=(const MemoryRoutineTest&) = delete;

  void SetUp() override {
    SetTestRoot(mock_context_.root_dir());
    routine_ = std::make_unique<MemoryRoutine>(
        &mock_context_, task_environment_.GetMockTickClock());
  }

  DiagnosticRoutine* routine() { return routine_.get(); }

  mojom::RoutineUpdate* update() { return &update_; }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

  void RunRoutineAndWaitForExit() {
    routine_->Start();

    // Since the memory routine has finished by the time Start() returns, there
    // is no need to wait.
    routine_->PopulateStatusUpdate(&update_, true);
  }

  mojom::RoutineUpdatePtr GetUpdate() {
    mojom::RoutineUpdate update{0, mojo::ScopedHandle(),
                                mojom::RoutineUpdateUnionPtr()};
    routine_->PopulateStatusUpdate(&update, true);
    return mojom::RoutineUpdate::New(update.progress_percent,
                                     std::move(update.output),
                                     std::move(update.routine_update_union));
  }

  void SetMockMemoryInfo(const std::string& info) {
    SetFile({"proc", "meminfo"}, info);
  }

  void SetExecutorResponse(int32_t exit_code,
                           const std::optional<std::string>& outfile_name,
                           const std::optional<base::TimeDelta>& delay) {
    SetMockMemoryInfo(
        "MemTotal:        3906320 kB\n"
        "MemFree:         2873180 kB\n"
        "MemAvailable:    2878980 kB\n");
    EXPECT_CALL(*mock_executor(), RunMemtester(_, _))
        .WillOnce(WithArg<1>(
            Invoke([=](mojom::Executor::RunMemtesterCallback callback) {
              mojom::ExecutedProcessResult result;
              result.return_code = exit_code;
              if (outfile_name.has_value()) {
                EXPECT_TRUE(base::ReadFileToString(
                    base::FilePath(kTestDataRoot).Append(outfile_name.value()),
                    &result.out));
              }
              if (!delay.has_value()) {
                std::move(callback).Run(result.Clone());
                return;
              }

              base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](mojom::Executor::RunMemtesterCallback callback,
                         mojom::ExecutedProcessResultPtr result) {
                        std::move(callback).Run(std::move(result));
                      },
                      std::move(callback), result.Clone()),
                  delay.value());
            })));
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
  mojom::RoutineUpdate update_{0, mojo::ScopedHandle(),
                               mojom::RoutineUpdateUnionPtr()};
};

// Test that we can create a memory routine with the default tick clock.
TEST_F(MemoryRoutineTest, DefaultTickClock) {
  MockContext mock_context;
  MemoryRoutine routine(&mock_context);

  EXPECT_EQ(routine.GetStatus(), mojom::DiagnosticRoutineStatusEnum::kReady);
}

// Test that the memory routine can run successfully.
TEST_F(MemoryRoutineTest, RoutineSuccess) {
  SetExecutorResponse(EXIT_SUCCESS, "good_memtester_output",
                      std::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kPassed,
                             kMemoryRoutineSucceededMessage);
  auto shm_mapping = diagnostics::GetReadOnlySharedMemoryMappingFromMojoHandle(
      std::move(update()->output));
  ASSERT_TRUE(shm_mapping.IsValid());
  EXPECT_EQ(std::string(shm_mapping.GetMemoryAs<const char>(),
                        shm_mapping.mapped_size()),
            ConstructOutput());
}

// Test that the memory routine handles the parsing error.
TEST_F(MemoryRoutineTest, RoutineParseError) {
  SetMockMemoryInfo("Incorrectly formatted meminfo contents.\n");

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojom::DiagnosticRoutineStatusEnum::kFailedToStart,
      kMemoryRoutineFetchingAvailableMemoryFailureMessage);
}

// Test that the memory routine handles the not having enough available memory
// error.
TEST_F(MemoryRoutineTest, RoutineNotEnoughAvailableMemory) {
  // MemAvailable less than 500 MiB.
  SetMockMemoryInfo(
      "MemTotal:        3906320 kB\n"
      "MemFree:         2873180 kB\n"
      "MemAvailable:    278980 kB\n");

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojom::DiagnosticRoutineStatusEnum::kFailedToStart,
      kMemoryRoutineNotHavingEnoughAvailableMemoryMessage);
}

// Test that the memory routine handles the memtester binary failing to run.
TEST_F(MemoryRoutineTest, MemtesterBinaryFailsToRun) {
  SetExecutorResponse(EXIT_FAILURE, std::nullopt /* outfile_name */,
                      std::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojom::DiagnosticRoutineStatusEnum::kError,
      kMemoryRoutineAllocatingLockingInvokingFailureMessage);
}

// Test that the memory routine handles a stuck address failure.
TEST_F(MemoryRoutineTest, StuckAddressFailure) {
  SetExecutorResponse(MemtesterErrorCodes::kStuckAddressTestError,
                      std::nullopt /* outfile_name */,
                      std::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kMemoryRoutineStuckAddressTestFailureMessage);
}

// Test that the memory routine handles a test failure other than stuck address.
TEST_F(MemoryRoutineTest, OtherTestFailure) {
  SetExecutorResponse(MemtesterErrorCodes::kOtherTestError,
                      std::nullopt /* outfile_name */,
                      std::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kFailed,
                             kMemoryRoutineOtherTestFailureMessage);
}

// Test that calling resume doesn't crash.
TEST_F(MemoryRoutineTest, Resume) {
  routine()->Resume();
}

// Test that the memory routine can be cancelled.
TEST_F(MemoryRoutineTest, Cancel) {
  base::TimeDelta time_delay = base::Seconds(10);
  SetExecutorResponse(EXIT_FAILURE, std::nullopt /* outfile_name */,
                      time_delay);

  routine()->Start();

  EXPECT_CALL(*mock_executor(), KillMemtester());

  routine()->Cancel();

  routine()->PopulateStatusUpdate(update(), false /* include_output */);

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kCancelled,
                             kMemoryRoutineCancelledMessage);

  // Make sure the original callback can't overwrite the cancelled status.
  FastForwardBy(time_delay);

  routine()->PopulateStatusUpdate(update(), false /* include_output */);

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojom::DiagnosticRoutineStatusEnum::kCancelled,
                             kMemoryRoutineCancelledMessage);
}

}  // namespace
}  // namespace diagnostics
