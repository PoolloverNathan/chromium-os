// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "germ/launcher.h"

#include <sys/types.h>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/minijail/minijail.h>

#include "germ/environment.h"

namespace {
const char* kSandboxedServiceTemplate = "germ_template";
const size_t kStdoutBufSize = 1024;
}  // namespace

namespace germ {

// Starts with the top half of user ids.
class UidService final {
 public:
  UidService() { min_uid_ = 1 << ((sizeof(uid_t) * 8) >> 1); }
  ~UidService() {}

  uid_t GetUid() {
    return static_cast<uid_t>(base::RandInt(min_uid_, 2 * min_uid_));
  }

 private:
  uid_t min_uid_;
};

Launcher::Launcher() {
  uid_service_.reset(new UidService());
}

Launcher::~Launcher() {}

bool Launcher::RunInteractiveCommand(const std::string& name,
                                     const std::vector<std::string>& argv,
                                     int* status) {
  std::vector<char*> cmdline;
  for (const auto& t : argv) {
    cmdline.push_back(const_cast<char*>(t.c_str()));
  }
  // Minijail will use the underlying char* array as 'argv',
  // so null-terminate it.
  cmdline.push_back(nullptr);
  return RunWithMinijail(cmdline, status);
}

bool Launcher::RunInteractiveSpec(const soma::ReadOnlyContainerSpec& spec,
                                  int* status) {
  std::vector<char*> cmdline;
  // TODO(jorgelo): support running more than one executable.
  for (const auto& t : spec.executables()[0]->command_line) {
    cmdline.push_back(const_cast<char*>(t.c_str()));
  }
  // Minijail will use the underlying char* array as 'argv',
  // so null-terminate it.
  cmdline.push_back(nullptr);
  return RunWithMinijail(cmdline, status);
}

bool Launcher::RunWithMinijail(const std::vector<char*>& cmdline, int* status) {
  chromeos::Minijail* minijail = chromeos::Minijail::GetInstance();

  uid_t uid = uid_service_->GetUid();
  Environment env(uid, uid);

  return minijail->RunSyncAndDestroy(env.GetForInteractive(), cmdline,
                                     status);
}

bool Launcher::RunDaemonized(const soma::ReadOnlyContainerSpec& spec,
                             pid_t* pid) {
  // initctl start germ_template NAME=yes ENVIRONMENT= COMMANDLINE=/usr/bin/yes
  std::vector<std::string> argv;
  // TODO(jorgelo): support running more than one executable.
  for (const auto& cmdline_token : spec.executables()[0]->command_line) {
    argv.push_back(cmdline_token);
  }

  uid_t uid = uid_service_->GetUid();
  Environment env(uid, uid);

  std::unique_ptr<chromeos::Process> initctl = GetProcessInstance();
  initctl->AddArg("/sbin/initctl");
  initctl->AddArg("start");
  initctl->AddArg(kSandboxedServiceTemplate);
  initctl->AddArg(base::StringPrintf("NAME=%s", spec.name().c_str()));
  initctl->AddArg(env.GetForService());
  std::string command_line = JoinString(argv, ' ');
  initctl->AddArg(base::StringPrintf("COMMANDLINE=%s", command_line.c_str()));
  initctl->RedirectUsingPipe(STDOUT_FILENO, false /* is_input */);

  // Since we're running 'initctl', and not the executable itself,
  // we wait for it to exit.
  initctl->Start();
  std::string output = ReadFromStdout(initctl.get());
  *pid = GetPidFromOutput(output);
  int exit_status = initctl->Wait();
  if (exit_status != 0) {
    LOG(ERROR) << "'initctl start' failed with exit status " << exit_status;
    *pid = -1;
    return false;
  }
  VLOG(1) << "service name " << spec.name() << " pid " << pid;
  names_[*pid] = spec.name();
  return true;
}

bool Launcher::Terminate(pid_t pid) {
  if (pid < 0) {
    LOG(ERROR) << "Invalid pid " << pid;
    return false;
  }

  if (names_.find(pid) == names_.end()) {
    LOG(ERROR) << "Unknown pid " << pid;
    return false;
  }

  std::string name = names_[pid];

  // initctl stop germ_template NAME=<name>
  std::unique_ptr<chromeos::Process> initctl = GetProcessInstance();
  initctl->AddArg("/sbin/initctl");
  initctl->AddArg("stop");
  initctl->AddArg(kSandboxedServiceTemplate);
  initctl->AddArg(base::StringPrintf("NAME=%s", name.c_str()));
  int exit_status = initctl->Run();
  if (exit_status != 0) {
    LOG(ERROR) << "'initctl stop' failed with exit status " << exit_status;
    return false;
  }
  names_.erase(pid);
  return true;
}

pid_t Launcher::GetPidFromOutput(const std::string& output) {
  // germ_template (test) start/running, process 8117
  std::vector<std::string> tokens;
  base::SplitString(output, ' ', &tokens);
  pid_t pid = -1;
  if (tokens.size() < 5 || !base::StringToInt(tokens[4], &pid)) {
    LOG(ERROR) << "Could not extract pid from '" << output << "'";
    return -1;
  }
  return pid;
}

std::string Launcher::ReadFromStdout(chromeos::Process* process) {
  int stdout_fd = process->GetPipe(STDOUT_FILENO);
  char buf[kStdoutBufSize] = {0};
  base::ReadFromFD(stdout_fd, buf, kStdoutBufSize - 1);
  std::string output(buf);
  return output;
}

std::unique_ptr<chromeos::Process> Launcher::GetProcessInstance() {
  std::unique_ptr<chromeos::Process> process(new chromeos::ProcessImpl());
  return process;
}

}  // namespace germ
