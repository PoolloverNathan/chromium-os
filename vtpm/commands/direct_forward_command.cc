// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/commands/direct_forward_command.h"

#include <string>
#include <utility>

#include <base/callback.h>
#include <base/check.h>
#include <trunks/trunks_factory.h>

namespace vtpm {

DirectForwardCommand::DirectForwardCommand(trunks::TrunksFactory* factory)
    : factory_(factory) {
  CHECK(factory_);
}

void DirectForwardCommand::Run(const std::string& command,
                               CommandResponseCallback callback) {
  const std::string response =
      factory_->GetTpmUtility()->SendCommandAndWait(command);

  std::move(callback).Run(response);
}

}  // namespace vtpm