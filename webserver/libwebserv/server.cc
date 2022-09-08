// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libwebserv/server.h>

#include <utility>

#include "libwebserv/dbus_server.h"

using std::unique_ptr;

namespace libwebserv {

unique_ptr<Server> Server::ConnectToServerViaDBus(
    const scoped_refptr<dbus::Bus>& bus,
    const std::string& service_name,
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb,
    const base::Closure& on_server_online,
    const base::Closure& on_server_offline) {
  DBusServer* server = new DBusServer;
  unique_ptr<Server> ret(server);
  server->Connect(bus, service_name, std::move(cb), on_server_online,
                  on_server_offline);
  return ret;
}

}  // namespace libwebserv
