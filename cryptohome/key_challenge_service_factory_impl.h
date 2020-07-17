// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_CHALLENGE_SERVICE_FACTORY_IMPL_H_
#define CRYPTOHOME_KEY_CHALLENGE_SERVICE_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_factory.h"

namespace cryptohome {

// Real implementation of the KeyChallengeServiceFactory interface that creates
// instances of KeyChallengeService that talk to the system D-Bus bus.
class KeyChallengeServiceFactoryImpl final : public KeyChallengeServiceFactory {
 public:
  KeyChallengeServiceFactoryImpl();
  KeyChallengeServiceFactoryImpl(const KeyChallengeServiceFactoryImpl&) =
      delete;
  KeyChallengeServiceFactoryImpl& operator=(
      const KeyChallengeServiceFactoryImpl&) = delete;
  ~KeyChallengeServiceFactoryImpl() override;

  std::unique_ptr<KeyChallengeService> New(
      scoped_refptr<::dbus::Bus> bus,
      const std::string& key_delegate_dbus_service_name) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_CHALLENGE_SERVICE_FACTORY_IMPL_H_
