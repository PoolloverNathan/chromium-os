// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_NV_SPACE_MANAGER_H_
#define VTPM_BACKENDS_NV_SPACE_MANAGER_H_

#include <string>

#include <trunks/tpm_generated.h>

namespace vtpm {

// `NvSpaceManager` manages the authorization, attributes, and data of one or
// more NV spaces.
class NvSpaceManager {
 public:
  virtual ~NvSpaceManager() = default;

  // Verifies the password for `nv_index`, and set `data` if the password
  // matches. The password authorization is by design directly defined by
  // implementation.
  virtual trunks::TPM_RC Read(trunks::TPM_NV_INDEX nv_index,
                              const std::string& password,
                              std::string& nv_data) = 0;
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_NV_SPACE_MANAGER_H_