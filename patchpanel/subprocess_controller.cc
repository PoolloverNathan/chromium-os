// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/subprocess_controller.h"

#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <brillo/syslog_logging.h>

namespace patchpanel {
namespace {
constexpr int kMaxRestarts = 5;
}  // namespace

void SubprocessController::Start(int argc,
                                 char* argv[],
                                 const std::string& fd_arg) {
  CHECK_GE(argc, 1);
  for (int i = 0; i < argc; i++) {
    argv_.push_back(argv[i]);
  }
  fd_arg_ = fd_arg;
  Launch();
}

bool SubprocessController::Restart() {
  if (++restarts_ > kMaxRestarts) {
    LOG(ERROR) << "Maximum number of restarts exceeded";
    return false;
  }
  LOG(INFO) << "Restarting...";
  Launch();
  return true;
}

void SubprocessController::Launch() {
  int control[2];

  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control) != 0) {
    PLOG(FATAL) << "socketpair failed";
  }

  base::ScopedFD control_fd(control[0]);
  msg_dispatcher_ =
      std::make_unique<MessageDispatcher>(std::move(control_fd), false);
  msg_dispatcher_->RegisterMessageHandler(base::BindRepeating(
      &SubprocessController::OnMessage, weak_factory_.GetWeakPtr()));
  const int subprocess_fd = control[1];

  std::vector<std::string> child_argv = argv_;
  child_argv.push_back(fd_arg_ + "=" + std::to_string(subprocess_fd));

  base::FileHandleMappingVector fd_mapping;
  fd_mapping.push_back({subprocess_fd, subprocess_fd});

  base::LaunchOptions options;
  options.fds_to_remap = std::move(fd_mapping);

  base::Process p = base::LaunchProcess(child_argv, options);
  CHECK(p.IsValid());
  pid_ = p.Pid();
}

void SubprocessController::SendControlMessage(
    const ControlMessage& proto) const {
  if (!msg_dispatcher_) {
    return;
  }
  SubprocessMessage msg;
  *msg.mutable_control_message() = proto;
  msg_dispatcher_->SendMessage(msg);
}

void SubprocessController::Listen() {
  if (!msg_dispatcher_) {
    return;
  }
  msg_dispatcher_->Start();
}

void SubprocessController::RegisterFeedbackMessageHandler(
    base::RepeatingCallback<void(const FeedbackMessage&)> handler) {
  feedback_handler_ = std::move(handler);
}

void SubprocessController::OnMessage(const SubprocessMessage& msg) {
  if (msg.has_feedback_message() && !feedback_handler_.is_null()) {
    feedback_handler_.Run(msg.feedback_message());
  }
}

}  // namespace patchpanel
