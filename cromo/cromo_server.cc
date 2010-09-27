// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cromo_server.h"

#include <chromeos/dbus/service_constants.h>

#include <dbus-c++/glib-integration.h>
#include <dbus/dbus.h>
#include <glog/logging.h>

#include "modem_handler.h"
#include "plugin_manager.h"

using std::vector;

const char* CromoServer::kServiceName = "org.chromium.ModemManager";
const char* CromoServer::kServicePath = "/org/chromium/ModemManager";

static const char* kDBusInterface = "org.freedesktop.DBus";
static const char* kDBusPath = "/org/freedesktop/DBus";
static const char* kDBusListNames = "ListNames";

CromoServer::CromoServer(DBus::Connection& connection)
    : DBus::ObjectAdaptor(connection, kServicePath),
      conn_(connection),
      powerd_up_(false),
      max_suspend_delay_(0) {
}

CromoServer::~CromoServer() {
  for (vector<ModemHandler*>::iterator it = modem_handlers_.begin();
       it != modem_handlers_.end(); it++) {
    delete *it;
  }
  modem_handlers_.clear();
}

vector<DBus::Path> CromoServer::EnumerateDevices(DBus::Error& error) {
  vector<DBus::Path> allpaths;

  for (vector<ModemHandler*>::iterator it = modem_handlers_.begin();
       it != modem_handlers_.end(); it++) {
    vector<DBus::Path> paths = (*it)->EnumerateDevices(error);
    allpaths.insert(allpaths.end(), paths.begin(), paths.end());
  }
  return allpaths;
}

void CromoServer::AddModemHandler(ModemHandler* handler) {
  LOG(INFO) << "AddModemHandler(" << handler->vendor_tag() << ")";
  modem_handlers_.push_back(handler);
}

void CromoServer::PowerDaemonUp() {
  LOG(INFO) << "Power daemon: up";
  if (!powerd_up_) {
    powerd_up_ = true;
    RegisterSuspendDelay();
  }
}

void CromoServer::PowerDaemonDown() {
  LOG(INFO) << "Power daemon: down";
  if (powerd_up_) {
    powerd_up_ = false;
  }
}

void CromoServer::CheckForPowerDaemon() {
  DBus::CallMessage msg;
  DBus::MessageIter iter;

  LOG(INFO) << "Checking for power daemon...";
  msg.destination(kDBusInterface);
  msg.interface(kDBusInterface);
  msg.member(kDBusListNames);
  msg.path(kDBusPath);
  DBus::Message ret = conn_.send_blocking(msg, -1);
  iter = ret.reader();
  iter = iter.recurse();
  while (!iter.at_end()) {
    if (!powerd_up_ &&
        strcmp(iter.get_string(), power_manager::kPowerManagerInterface) == 0) {
      PowerDaemonUp();
    }
    iter++;
  }
}

void CromoServer::SuspendReady() {
  DBus::SignalMessage msg("/", power_manager::kPowerManagerInterface,
                          power_manager::kSuspendReady);
  LOG(INFO) << "SuspendReady: " << suspend_nonce_;
  msg.destination(power_manager::kPowerManagerInterface);
  msg.writer().append_uint32(suspend_nonce_);
  conn_.send(msg, NULL);
}

bool CromoServer::CheckSuspendReady() {
  bool okay_to_suspend = suspend_ok_hooks_.Run();
  if (okay_to_suspend) {
    SuspendReady();
  }
  // We return whether we need to run again. We only run again if the suspend
  // wasn't ready.
  return okay_to_suspend;
}

gboolean test_for_suspend(void *arg) {
  CromoServer *srv = static_cast<CromoServer*>(arg);
  return !srv->CheckSuspendReady();
}

void CromoServer::SuspendDelay(unsigned int nonce) {
  LOG(INFO) << "SuspendDelay: " << nonce;
  suspend_nonce_ = nonce;
  start_suspend_hooks_.Run();
  if (CheckSuspendReady()) {
    return;
  }
  g_timeout_add_seconds(1, test_for_suspend, static_cast<void*>(this));
}

void CromoServer::RegisterStartSuspend(const std::string& name,
                                       bool (*func)(void *), void *arg,
                                       unsigned int maxdelay) {
  suspend_delays_[name] = maxdelay;
  if (max_suspend_delay_ < maxdelay)
    max_suspend_delay_ = maxdelay;
  start_suspend_hooks_.Add(name, func, arg);
  if (powerd_up_)
    RegisterSuspendDelay();
}

void CromoServer::RegisterSuspendDelay() {
  DBus::CallMessage* call = new DBus::CallMessage();
  call->destination(power_manager::kPowerManagerInterface);
  call->interface(power_manager::kPowerManagerInterface);
  call->path("/");
  call->member(power_manager::kRegisterSuspendDelay);
  call->append(DBUS_TYPE_UINT32, &max_suspend_delay_, DBUS_TYPE_INVALID);
  call->terminate();
  DBus::Message reply = conn_.send_blocking(*call, -1);
  if (reply.is_error())
    LOG(WARNING) << "Can't register for suspend delay: " << max_suspend_delay_;
  else
    LOG(INFO) << "Registered for suspend delay: " << max_suspend_delay_;
}

void CromoServer::UnregisterStartSuspend(const std::string& name) {
  suspend_delays_.erase(name);
  start_suspend_hooks_.Del(name);
  max_suspend_delay_ = MaxSuspendDelay();
  RegisterSuspendDelay();
}

unsigned int CromoServer::MaxSuspendDelay() {
  SuspendDelayMap::iterator it;
  unsigned int max = 0;
  for (it = suspend_delays_.begin(); it != suspend_delays_.end(); it++) {
    if (it->second > max)
      max = it->second;
  }
  return max;
}
