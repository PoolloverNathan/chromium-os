// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCROSSYSTEM_CROSSYSTEM_H_
#define LIBCROSSYSTEM_CROSSYSTEM_H_

#include <cstddef>
#include <optional>

#include <string>

#include <brillo/brillo_export.h>

namespace crossystem {

// C++ interface to access crossystem system properties.
class BRILLO_EXPORT Crossystem {
 public:
  virtual ~Crossystem() = default;

  // Reads a system property integer.
  //
  // @param name The name of the target system property.
  // @return The property value, or |std::nullopt| if error.
  virtual std::optional<int> VbGetSystemPropertyInt(
      const std::string& name) const = 0;

  // Sets a system property integer.
  //
  // @param name The name of the target system property.
  // @param value The integer value to set.
  // @return |true| if it succeeds; |false| if it fails.
  virtual bool VbSetSystemPropertyInt(const std::string& name, int value) = 0;

  // Reads a system property string.
  //
  // @param name The name of the target system property.
  // @return The property value, or |std::nullopt| if error.
  virtual std::optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const = 0;

  // Sets a system property string.
  //
  // @param name The name of the target system property.
  // @param value The string value to set.
  // @return |true| if it succeeds; |false| if it fails.
  virtual bool VbSetSystemPropertyString(const std::string& name,
                                         const std::string& value) = 0;
};

// The sole implementation that invokes the corresponding functions provided
// in vboot/crossystem.h .
class BRILLO_EXPORT CrossystemImpl : public Crossystem {
 public:
  std::optional<int> VbGetSystemPropertyInt(
      const std::string& name) const override;

  bool VbSetSystemPropertyInt(const std::string& name, int value) override;

  std::optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const override;

  bool VbSetSystemPropertyString(const std::string& name,
                                 const std::string& value) override;
};

}  // namespace crossystem

#endif  // LIBCROSSYSTEM_CROSSYSTEM_H_
