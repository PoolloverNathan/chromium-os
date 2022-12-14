// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_CONFIG_LOADER_H_
#define RUNTIME_PROBE_PROBE_CONFIG_LOADER_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/values.h>

namespace runtime_probe {

struct ProbeConfigData {
  base::FilePath path;
  base::Value config;
  std::string sha1_hash;
};

// Interface that provides ways to load probe config from files.

class ProbeConfigLoader {
 public:
  virtual ~ProbeConfigLoader() = default;

  // Loads probe config from the default path.
  virtual std::optional<ProbeConfigData> LoadDefault() const = 0;

  // Loads probe config from the given path.
  virtual std::optional<ProbeConfigData> LoadFromFile(
      const base::FilePath& file_path) const = 0;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_CONFIG_LOADER_H_
