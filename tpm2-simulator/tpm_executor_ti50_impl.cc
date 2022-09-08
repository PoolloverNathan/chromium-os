// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>

// Needs to be included after sys/socket.h
#include <linux/un.h>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/posix/unix_domain_socket.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <linux/vtpm_proxy.h>

#include "tpm2-simulator/constants.h"
#include "tpm2-simulator/tpm_command_utils.h"
#include "tpm2-simulator/tpm_executor_ti50_impl.h"
#include "tpm2-simulator/tpm_vendor_cmd_locality.h"

namespace tpm2_simulator {

namespace {

constexpr base::TimeDelta kPollingInterval = base::Milliseconds(100);

constexpr char kGpioPltRstFile[] = "gpioPltRst";
constexpr char kTpmFifoFile[] = "direct_tpm_fifo";
constexpr char kOne[] = "1";

constexpr char kTi50EmulatorKernel[] = "/usr/bin/ti50-emulator-kernel";
constexpr char const* kTi50EmulatorApps[] = {
    "/usr/bin/ti50-emulator-fw_updater", "/usr/bin/ti50-emulator-tpm2",
    "/usr/bin/ti50-emulator-sys_mgr",    "/usr/bin/ti50-emulator-ec_comm",
    "/usr/bin/ti50-emulator-u2f",        "/usr/bin/ti50-emulator-pinweaver",
};

constexpr size_t kBufferSize = 1024;

bool ToSockAddr(const base::FilePath& path, struct sockaddr_un* sa) {
  // sun_path needs to include trailing '\0' byte.
  if (path.value().size() >= sizeof(sa->sun_path)) {
    LOG(ERROR) << "Path is too long: " << path.value();
    return false;
  }

  memset(sa, 0, sizeof(*sa));
  sa->sun_family = AF_UNIX;
  strncpy(sa->sun_path, path.value().c_str(), sizeof(sa->sun_path) - 1);
  return true;
}

std::pair<int, base::ScopedFD> ConnectUnixDomainSocket(
    const base::FilePath& path) {
  struct sockaddr_un sa;
  if (!ToSockAddr(path, &sa))
    return std::make_pair(EFAULT, base::ScopedFD());

  base::ScopedFD fd(
      socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    int result_errno = errno;
    PLOG(ERROR) << "Failed to create unix domain socket";
    return std::make_pair(result_errno, base::ScopedFD());
  }

  if (HANDLE_EINTR(connect(fd.get(),
                           reinterpret_cast<const struct sockaddr*>(&sa),
                           sizeof(sa))) == -1) {
    int result_errno = errno;
    PLOG(ERROR) << "Failed to connect.";
    return std::make_pair(result_errno, base::ScopedFD());
  }

  return std::make_pair(0, std::move(fd));
}

void RemoveSocketFiles() {
  base::FileEnumerator emt(base::FilePath("."), true,
                           base::FileEnumerator::FILES);
  for (base::FilePath path = emt.Next(); !path.empty(); path = emt.Next()) {
    base::stat_wrapper_t statbuf;
    if (base::File::Lstat(path.value().c_str(), &statbuf) != 0) {
      PLOG(ERROR) << "Failed to lstat " << path.value();
      continue;
    }
    if (S_ISSOCK(statbuf.st_mode)) {
      if (!base::DeleteFile(path)) {
        LOG(ERROR) << "Failed to delete " << path.value();
      }
    }
  }
}

}  // namespace

TpmExecutorTi50Impl::TpmExecutorTi50Impl() {
  vendor_commands_.emplace_back(std::make_unique<TpmVendorCommandLocality>());
}

TpmExecutorTi50Impl::~TpmExecutorTi50Impl() {
  process_.Wait();
}

void TpmExecutorTi50Impl::InitializeVTPM() {
  RemoveSocketFiles();

  for (const auto& vendor_cmd : vendor_commands_) {
    if (!vendor_cmd->Init()) {
      LOG(ERROR) << "Failed to initialize vendor command.";
    }
  }

  process_.AddArg(kTi50EmulatorKernel);

  // Use current dir.
  process_.AddArg("--path");
  process_.AddArg(".");

  for (const char* app : kTi50EmulatorApps) {
    process_.AddArg("-a");
    process_.AddArg(app);
  }

  if (!process_.Start()) {
    LOG(ERROR) << "Failed to start ti50 emulation.";
    return;
  }

  base::FilePath gpio(kGpioPltRstFile);
  base::FilePath tpm_fifo(kTpmFifoFile);

  while (!base::PathExists(gpio) || !base::PathExists(tpm_fifo)) {
    // TODO(yich): Don't use polling.
    base::PlatformThread::Sleep(kPollingInterval);
  }

  auto pair = ConnectUnixDomainSocket(gpio);
  if (pair.first != 0) {
    LOG(ERROR) << "Failed to connect to ti50 emulation.";
    return;
  }

  if (!base::UnixDomainSocket::SendMsg(pair.second.get(), kOne, std::size(kOne),
                                       {pair.second.get()})) {
    LOG(ERROR) << "Failed to send all data over socket.";
    return;
  }

  LOG(INFO) << "vTPM Initialize.";
}

size_t TpmExecutorTi50Impl::GetCommandSize(const std::string& command) {
  uint32_t size;
  if (!ExtractCommandSize(command, &size)) {
    LOG(ERROR) << "Command too small.";
    return command.size();
  }
  return size;
}

std::string TpmExecutorTi50Impl::RunCommand(const std::string& command) {
  for (const auto& vendor_cmd : vendor_commands_) {
    if (vendor_cmd->IsVendorCommand(command)) {
      return vendor_cmd->RunCommand(command);
    }
  }

  auto pair = ConnectUnixDomainSocket(base::FilePath(kTpmFifoFile));
  if (pair.first != 0) {
    LOG(ERROR) << "Failed to connect to ti50 TPM FIFO.";
    return CreateCommandWithCode(0xdead);
  }

  if (!base::UnixDomainSocket::SendMsg(pair.second.get(), command.data(),
                                       command.length(), {pair.second.get()})) {
    LOG(ERROR) << "Failed to send all data over socket.";
    return CreateCommandWithCode(0xdead);
  }

  char buffer[kBufferSize];
  std::string result;
  while (true) {
    uint32_t size;
    if (ExtractCommandSize(result, &size) && result.size() >= size) {
      break;
    }

    std::vector<base::ScopedFD> fds;
    ssize_t len = base::UnixDomainSocket::RecvMsg(pair.second.get(), buffer,
                                                  sizeof(buffer), &fds);
    result += std::string(buffer, len);

    if (len < 0) {
      break;
    }
  }

  return result;
}

}  // namespace tpm2_simulator
