// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dns_client_factory.h"

#include <memory>

namespace shill {

DnsClientFactory::DnsClientFactory() = default;
DnsClientFactory::~DnsClientFactory() = default;

DnsClientFactory* DnsClientFactory::GetInstance() {
  static base::NoDestructor<DnsClientFactory> instance;
  return instance.get();
}

std::unique_ptr<DnsClient> DnsClientFactory::CreateDnsClient(
    IPAddress::Family family,
    const std::string& interface_name,
    const std::vector<std::string>& dns_servers,
    int timeout_ms,
    EventDispatcher* dispatcher,
    const DnsClient::ClientCallback& callback) {
  return std::make_unique<DnsClient>(family,
                                     interface_name,
                                     dns_servers,
                                     timeout_ms,
                                     dispatcher,
                                     callback);
}

}  // namespace shill
