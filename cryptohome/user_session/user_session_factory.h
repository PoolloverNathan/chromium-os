// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_USER_SESSION_FACTORY_H_
#define CRYPTOHOME_USER_SESSION_USER_SESSION_FACTORY_H_

#include <memory>
#include <string>

#include "cryptohome/user_session/user_session.h"

namespace cryptohome {

class UserSessionFactory {
 public:
  UserSessionFactory() = default;
  virtual ~UserSessionFactory() = default;

  virtual std::unique_ptr<UserSession> New(const std::string& username,
                                           bool legacy_mount,
                                           bool bind_mount_downloads) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_USER_SESSION_FACTORY_H_
