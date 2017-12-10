// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_service.h"

#include <dbus/dbus.h>  // C dbus library header. Used in FilterMessage().
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/switches/chrome_switches.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/scoped_dbus_error.h>

#include "login_manager/browser_job.h"
#include "login_manager/child_exit_handler.h"
#include "login_manager/container_manager_impl.h"
#include "login_manager/key_generator.h"
#include "login_manager/liveness_checker_impl.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/session_manager_impl.h"
#include "login_manager/system_utils.h"
#include "login_manager/systemd_unit_starter.h"
#include "login_manager/termina_manager_impl.h"
#include "login_manager/upstart_signal_emitter.h"

namespace em = enterprise_management;
namespace login_manager {

namespace {

const int kSignals[] = {SIGTERM, SIGINT, SIGHUP};
const int kNumSignals = sizeof(kSignals) / sizeof(int);

// The only path where containers are allowed to be installed.  They must be
// part of the read-only, signed root image.
const char kContainerInstallDirectory[] = "/opt/google/containers";

// The path where the pid of an aborted browser process is written. This is done
// so that crash reporting tools can detect an abort that originated from
// session_manager.
const char kAbortedBrowserPidPath[] = "/run/chrome/aborted_browser_pid";

// I need a do-nothing action for SIGALRM, or using alarm() will kill me.
void DoNothing(int signal) {}

}  // anonymous namespace

// TODO(mkrebs): Remove CollectChrome timeout and file when
// crosbug.com/5872 is fixed.
// When crash-reporter based crash reporting of Chrome is enabled
// (which should only be during test runs) we use
// kKillTimeoutCollectChrome instead of the kill timeout specified at
// the command line.
const int SessionManagerService::kKillTimeoutCollectChrome = 60;
const char SessionManagerService::kCollectChromeFile[] =
    "/mnt/stateful_partition/etc/collect_chrome_crashes";

void SessionManagerService::TestApi::ScheduleChildExit(pid_t pid, int status) {
  siginfo_t info;
  info.si_pid = pid;
  if (WIFEXITED(status)) {
    info.si_code = CLD_EXITED;
    info.si_status = WEXITSTATUS(status);
  } else {
    info.si_status = WTERMSIG(status);
  }
  brillo::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&SessionManagerService::HandleExit,
                            session_manager_service_, info));
}

SessionManagerService::SessionManagerService(
    std::unique_ptr<BrowserJobInterface> child_job,
    uid_t uid,
    int kill_timeout,
    bool enable_browser_abort_on_hang,
    base::TimeDelta hang_detection_interval,
    LoginMetrics* metrics,
    SystemUtils* utils)
    : browser_(std::move(child_job)),
      exit_on_child_done_(false),
      kill_timeout_(base::TimeDelta::FromSeconds(kill_timeout)),
      match_rule_(base::StringPrintf("type='method_call', interface='%s'",
                                     kSessionManagerInterface)),
      chrome_dbus_proxy_(nullptr),
      powerd_dbus_proxy_(nullptr),
      login_metrics_(metrics),
      system_(utils),
      nss_(NssUtil::Create()),
      owner_key_(nss_->GetOwnerKeyFilePath(), nss_.get()),
      key_gen_(uid, utils),
      state_key_generator_(utils, metrics),
      vpd_process_(utils),
#if USE_ANDROID_MASTER_CONTAINER
      android_container_(std::make_unique<AndroidOciWrapper>(
          utils, base::FilePath(kContainerInstallDirectory))),
#else   // USE_ANDROID_MASTER_CONTAINER
      android_container_(std::make_unique<AndroidContainerManagerImpl>(
          utils,
          base::FilePath(kContainerInstallDirectory),
          SessionManagerImpl::kArcContainerName)),
#endif  // USE_ANDROID_MASTER_CONTAINER
      termina_manager_(std::make_unique<TerminaManagerImpl>(utils)),
      enable_browser_abort_on_hang_(enable_browser_abort_on_hang),
      liveness_checking_interval_(hang_detection_interval),
      aborted_browser_pid_path_(kAbortedBrowserPidPath),
      shutting_down_(false),
      shutdown_already_(false),
      exit_code_(SUCCESS) {
  DCHECK(browser_);
  SetUpHandlers();
}

SessionManagerService::~SessionManagerService() {
  RevertHandlers();
}

bool SessionManagerService::Initialize() {
  LOG(INFO) << "SessionManagerService starting";
  InitializeDBus();

  chrome_dbus_proxy_ =
      bus_->GetObjectProxy(chromeos::kLibCrosServiceName,
                           dbus::ObjectPath(chromeos::kLibCrosServicePath));

  powerd_dbus_proxy_ = bus_->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));

  dbus::ObjectProxy* component_updater_proxy = bus_->GetObjectProxy(
      chromeos::kComponentUpdaterServiceName,
      dbus::ObjectPath(chromeos::kComponentUpdaterServicePath));

  dbus::ObjectProxy* system_clock_proxy = bus_->GetObjectProxy(
      system_clock::kSystemClockServiceName,
      dbus::ObjectPath(system_clock::kSystemClockServicePath));

#if USE_SYSTEMD
  using InitDaemonControllerImpl = SystemdUnitStarter;
#else
  using InitDaemonControllerImpl = UpstartSignalEmitter;
#endif
  dbus::ObjectProxy* init_dbus_proxy =
      bus_->GetObjectProxy(InitDaemonControllerImpl::kServiceName,
                           dbus::ObjectPath(InitDaemonControllerImpl::kPath));

  liveness_checker_.reset(new LivenessCheckerImpl(this, chrome_dbus_proxy_,
                                                  enable_browser_abort_on_hang_,
                                                  liveness_checking_interval_));

  // Initially store in derived-type pointer, so that we can initialize
  // appropriately below.
  impl_ = std::make_unique<SessionManagerImpl>(
      this /* delegate */,
      std::make_unique<InitDaemonControllerImpl>(init_dbus_proxy), bus_,
      &key_gen_, &state_key_generator_,
      this /* manager, i.e. ProcessManagerServiceInterface */, login_metrics_,
      nss_.get(), system_, &crossystem_, &vpd_process_, &owner_key_,
      android_container_.get(), termina_manager_.get(),
      &install_attributes_reader_, component_updater_proxy, system_clock_proxy);
  if (!InitializeImpl())
    return false;

  // Set any flags that were specified system-wide.
  browser_->SetExtraArguments(impl_->GetStartUpFlags());

  CHECK(impl_->StartDBusService())
      << "Unable to start " << kSessionManagerServiceName << " D-Bus service.";
  return true;
}

void SessionManagerService::Finalize() {
  LOG(INFO) << "SessionManagerService exiting";
  impl_->Finalize();
  ShutDownDBus();
}

void SessionManagerService::LockScreen() {
  dbus::MethodCall call(chromeos::kLibCrosServiceInterface,
                        chromeos::kLockScreen);
  chrome_dbus_proxy_->CallMethod(&call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                                 dbus::ObjectProxy::EmptyResponseCallback());
}

void SessionManagerService::RestartDevice(const std::string& description) {
  dbus::MethodCall call(power_manager::kPowerManagerInterface,
                        power_manager::kRequestRestartMethod);
  dbus::MessageWriter writer(&call);
  writer.AppendInt32(power_manager::REQUEST_RESTART_OTHER);
  writer.AppendString(description);
  powerd_dbus_proxy_->CallMethodAndBlock(
      &call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
}

void SessionManagerService::ScheduleShutdown() {
  SetExitAndScheduleShutdown(SUCCESS);
}

void SessionManagerService::RunBrowser() {
  browser_->RunInBackground();
  DLOG(INFO) << "Browser is " << browser_->CurrentPid();
  liveness_checker_->Start();
  // Note that |child_exit_handler_| will catch browser process termination and
  // call HandleExit().
}

void SessionManagerService::AbortBrowser(int signal,
                                         const std::string& message) {
  std::string pid_string = base::IntToString(browser_->CurrentPid());
  base::WriteFile(aborted_browser_pid_path_, pid_string.c_str(),
                  pid_string.size());

  // Change the file to be owned by the user and group of the containing
  // directory. crash_reporter, which reads this file, is run by chrome using
  // the chronos user.
  struct stat stat_buf;
  if (stat(aborted_browser_pid_path_.DirName().value().c_str(), &stat_buf) <
      0) {
    PLOG(ERROR) << "Could not stat: "
                << aborted_browser_pid_path_.DirName().value();
  } else {
    if (chown(aborted_browser_pid_path_.value().c_str(), stat_buf.st_uid,
              stat_buf.st_gid) < 0) {
      PLOG(ERROR) << "Could not chown: " << aborted_browser_pid_path_.value();
    }
  }

  browser_->Kill(signal, message);
  browser_->WaitAndAbort(GetKillTimeout());
}

void SessionManagerService::RestartBrowserWithArgs(
    const std::vector<std::string>& args,
    bool args_are_extra,
    const std::vector<std::string>& env_vars) {
  // Waiting for Chrome to shutdown takes too much time.
  // We're killing it immediately hoping that data Chrome uses before
  // logging in is not corrupted.
  // TODO(avayvod): Remove RestartJob when crosbug.com/6924 is fixed.
  if (browser_->CurrentPid() > 0)
    browser_->KillEverything(SIGKILL, "Restarting browser on-demand.");
  if (args_are_extra)
    browser_->SetExtraArguments(args);
  else
    browser_->SetArguments(args);
  browser_->SetExtraEnvironmentVariables(env_vars);
  // The browser will be restarted in HandleExit().
}

void SessionManagerService::SetBrowserSessionForUser(
    const std::string& account_id, const std::string& userhash) {
  browser_->StartSession(account_id, userhash);
}

void SessionManagerService::SetFlagsForUser(
    const std::string& account_id, const std::vector<std::string>& flags) {
  browser_->SetExtraArguments(flags);
}

bool SessionManagerService::IsBrowser(pid_t pid) {
  return (browser_->CurrentPid() > 0 && pid == browser_->CurrentPid());
}

bool SessionManagerService::IsManagedJob(pid_t pid) {
  return IsBrowser(pid);
}

void SessionManagerService::HandleExit(const siginfo_t& ignored) {
  LOG(INFO) << "Exiting process is " << browser_->GetName() << ".";

  // Clears up the whole job's process group.
  browser_->KillEverything(SIGKILL, "Ensuring browser processes are gone.");
  browser_->WaitAndAbort(GetKillTimeout());
  browser_->ClearPid();

  // Also ensure all containers are gone.
  android_container_->RequestJobExit();
  android_container_->EnsureJobExit(SessionManagerImpl::kContainerTimeout);
  termina_manager_->RequestJobExit();
  termina_manager_->EnsureJobExit(SessionManagerImpl::kContainerTimeout);

  // Do nothing if already shutting down.
  if (shutting_down_)
    return;

  liveness_checker_->Stop();

  if (impl_->ShouldEndSession()) {
    LOG(ERROR) << "Choosing to end session rather than restart browser.";
    SetExitAndScheduleShutdown(CRASH_WHILE_RESTART_DISABLED);
    return;
  }

  if (browser_->ShouldStop()) {
    LOG(WARNING) << "Child stopped, shutting down";
    SetExitAndScheduleShutdown(CHILD_EXITING_TOO_FAST);
  } else if (browser_->ShouldRunBrowser()) {
    // TODO(cmasone): deal with fork failing in RunBrowser()
    RunBrowser();
  } else {
    LOG(INFO) << "Should NOT run " << browser_->GetName() << " again.";
    AllowGracefulExitOrRunForever();
  }
}

void SessionManagerService::RequestJobExit() {
  if (browser_->CurrentPid() > 0)
    browser_->Kill(SIGTERM, "");
}

void SessionManagerService::EnsureJobExit(base::TimeDelta timeout) {
  if (browser_->CurrentPid() > 0)
    browser_->WaitAndAbort(timeout);
}

DBusHandlerResult SessionManagerService::FilterMessage(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* data) {
  SessionManagerService* service = static_cast<SessionManagerService*>(data);
  if (::dbus_message_is_method_call(message, kSessionManagerInterface,
                                    kSessionManagerRestartJob)) {
    const char* sender = ::dbus_message_get_sender(message);
    if (!sender) {
      LOG(ERROR) << "Call to RestartJob has no sender";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    LOG(INFO) << "Received RestartJob from " << sender;
    DBusMessage* get_pid = ::dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetConnectionUnixProcessID");
    CHECK(get_pid);
    ::dbus_message_append_args(get_pid, DBUS_TYPE_STRING, &sender,
                               DBUS_TYPE_INVALID);
    DBusMessage* got_pid =
        ::dbus_connection_send_with_reply_and_block(conn, get_pid, -1, NULL);
    ::dbus_message_unref(get_pid);
    if (!got_pid) {
      LOG(ERROR) << "Could not look up sender of RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    uint32_t pid;
    if (!::dbus_message_get_args(got_pid, NULL, DBUS_TYPE_UINT32, &pid,
                                 DBUS_TYPE_INVALID)) {
      ::dbus_message_unref(got_pid);
      LOG(ERROR) << "Could not extract pid of sender of RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    ::dbus_message_unref(got_pid);
    if (!service->IsBrowser(pid)) {
      LOG(WARNING) << "Sender of RestartJob is no child of mine!";
      DBusMessage* denial = dbus_message_new_error(
          message, DBUS_ERROR_ACCESS_DENIED, "Sender is not browser.");
      if (!denial || !::dbus_connection_send(conn, denial, NULL))
        LOG(ERROR) << "Could not create error response to RestartJob.";
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void SessionManagerService::SetUpHandlers() {
  // I have to ignore SIGUSR1, because Xorg sends it to this process when it's
  // got no clients and is ready for new ones.  If we don't ignore it, we die.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = SIG_IGN;
  CHECK_EQ(sigaction(SIGUSR1, &action, NULL), 0);

  action.sa_handler = DoNothing;
  CHECK_EQ(sigaction(SIGALRM, &action, NULL), 0);

  std::vector<JobManagerInterface*> job_managers;
  job_managers.push_back(this);
  job_managers.push_back(&key_gen_);
  job_managers.push_back(&vpd_process_);
  job_managers.push_back(android_container_.get());
  job_managers.push_back(termina_manager_.get());
  signal_handler_.Init();
  child_exit_handler_.Init(&signal_handler_, job_managers);
  for (int i = 0; i < kNumSignals; ++i) {
    signal_handler_.RegisterHandler(
        kSignals[i], base::Bind(&SessionManagerService::OnTerminationSignal,
                                base::Unretained(this)));
  }
}

void SessionManagerService::RevertHandlers() {
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  RAW_CHECK(sigaction(SIGUSR1, &action, NULL) == 0);
  RAW_CHECK(sigaction(SIGALRM, &action, NULL) == 0);
}

base::TimeDelta SessionManagerService::GetKillTimeout() {
  if (base::PathExists(base::FilePath(kCollectChromeFile)))
    return base::TimeDelta::FromSeconds(kKillTimeoutCollectChrome);
  else
    return kill_timeout_;
}

bool SessionManagerService::InitializeImpl() {
  if (!impl_->Initialize()) {
    LOG(ERROR) << "Policy key is likely corrupt. Initiating device wipe.";
    impl_->InitiateDeviceWipe("bad_policy_key");
    impl_->Finalize();
    exit_code_ = MUST_WIPE_DEVICE;
    return false;
  }
  return true;
}

void SessionManagerService::InitializeDBus() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  CHECK(bus_->Connect());
  CHECK(bus_->SetUpAsyncOperations());

  bus_->AddFilterFunction(&SessionManagerService::FilterMessage, this);
  dbus::ScopedDBusError error;
  bus_->AddMatch(match_rule_, error.get());
  CHECK(!error.is_set()) << "Failed to add match to bus: " << error.name()
                         << ", message="
                         << (error.message() ? error.message() : "unknown.");
}

void SessionManagerService::ShutDownDBus() {
  dbus::ScopedDBusError error;
  bus_->RemoveMatch(match_rule_, error.get());
  if (error.is_set()) {
    LOG(ERROR) << "Failed to remove match from bus: " << error.name()
               << ", message="
               << (error.message() ? error.message() : "unknown.");
  }
  bus_->RemoveFilterFunction(&SessionManagerService::FilterMessage, this);
  bus_->ShutdownAndBlock();
}

void SessionManagerService::AllowGracefulExitOrRunForever() {
  if (exit_on_child_done_) {
    LOG(INFO) << "SessionManagerService set to exit on child done";
    brillo::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(base::IgnoreResult(&SessionManagerService::ScheduleShutdown),
                   this));
  } else {
    DLOG(INFO) << "OK, running forever...";
  }
}

void SessionManagerService::SetExitAndScheduleShutdown(ExitCode code) {
  shutting_down_ = true;
  exit_code_ = code;
  impl_->AnnounceSessionStoppingIfNeeded();

  child_exit_handler_.Reset();
  liveness_checker_->Stop();
  CleanupChildren(GetKillTimeout());
  impl_->AnnounceSessionStopped();

  brillo::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&brillo::MessageLoop::BreakLoop,
                            base::Unretained(brillo::MessageLoop::current())));
  LOG(INFO) << "SessionManagerService quitting run loop";
}

void SessionManagerService::CleanupChildren(base::TimeDelta timeout) {
  RequestJobExit();
  key_gen_.RequestJobExit();
  android_container_->RequestJobExit();
  termina_manager_->RequestJobExit();
  EnsureJobExit(timeout);
  key_gen_.EnsureJobExit(timeout);
  termina_manager_->EnsureJobExit(SessionManagerImpl::kContainerTimeout);
}

bool SessionManagerService::OnTerminationSignal(
    const struct signalfd_siginfo& info) {
  ScheduleShutdown();
  return true;
}

}  // namespace login_manager
