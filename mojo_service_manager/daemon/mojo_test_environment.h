// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_MOJO_TEST_ENVIRONMENT_H_
#define MOJO_SERVICE_MANAGER_DAEMON_MOJO_TEST_ENVIRONMENT_H_

#include <base/test/task_environment.h>
#include <base/threading/thread_task_runner_handle.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

namespace chromeos {
namespace mojo_service_manager {

// Sets up test environment for mojo.
class MojoTaskEnvironment : public base::test::SingleThreadTaskEnvironment {
 public:
  template <class... ArgTypes>
  explicit MojoTaskEnvironment(ArgTypes... args)
      : base::test::SingleThreadTaskEnvironment(MainThreadType::IO, args...),
        ipc_support_(base::ThreadTaskRunnerHandle::Get(),
                     mojo::core::ScopedIPCSupport::ShutdownPolicy::
                         CLEAN /* blocking shutdown */) {}

 private:
  mojo::core::ScopedIPCSupport ipc_support_;
};

}  // namespace mojo_service_manager
}  // namespace chromeos

#endif  // MOJO_SERVICE_MANAGER_DAEMON_MOJO_TEST_ENVIRONMENT_H_
