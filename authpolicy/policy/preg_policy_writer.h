// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUTHPOLICY_POLICY_PREG_POLICY_WRITER_H_
#define AUTHPOLICY_POLICY_PREG_POLICY_WRITER_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "authpolicy/policy/user_policy_encoder.h"

namespace policy {

// Helper class to write valid registry.pol ("PREG") files with the specified
// policy values. Useful to test preg parsing and encoding for in unit tests.
// See https://msdn.microsoft.com/en-us/library/aa374407(v=vs.85).aspx for a
// description of the file format.
class PRegPolicyWriter {
 public:
  // Creates a new writer using |mandatory_key| as registry key for mandatory
  // policies and |recommended_key| for recommended policies.
  PRegPolicyWriter(const std::string& mandatory_key,
                   const std::string& recommended_key);
  ~PRegPolicyWriter();

  // Sets the registry key used for mandatory policies.
  void SetMandatoryKey(const std::string& mandatory_key) {
    mandatory_key_ = mandatory_key;
  }

  // Sets the registry key used for recommended policies.
  void SetRecommendedKey(const std::string& recommended_key) {
    recommended_key_ = recommended_key;
  }

  // Appends a boolean policy value.
  void AppendBoolean(const char* policy_name,
                     bool value,
                     PolicyLevel level = POLICY_LEVEL_MANDATORY);

  // Appends an integer policy value.
  void AppendInteger(const char* policy_name,
                     uint32_t value,
                     PolicyLevel level = POLICY_LEVEL_MANDATORY);

  // Appends a string policy value.
  void AppendString(const char* policy_name,
                    const std::string& value,
                    PolicyLevel level = POLICY_LEVEL_MANDATORY);

  // Appends a string list policy value.
  void AppendStringList(const char* policy_name,
                        const std::vector<std::string>& values,
                        PolicyLevel level = POLICY_LEVEL_MANDATORY);

  // Writes the policy data to a file. Returns true on success.
  bool WriteToFile(const base::FilePath& path);

 protected:
  // Constructor for derived classes that set custom registry keys.
  PRegPolicyWriter();

 private:
  // Starts a policy entry. Entries have the shape '[key;value;type;size;data]'.
  // This method writes '[key;value;type;size;'.
  void StartEntry(const std::string& key_name,
                  const std::string& value_name,
                  uint32_t data_type,
                  uint32_t data_size);

  // Ends a policy entry (writes ']'). The caller has to fill in the data
  // between StartEntry() and EndEntry().
  void EndEntry();

  // Appends a NULL terminated string to the internal buffer. Note that all
  // strings are written as char16s.
  void AppendNullTerminatedString(const std::string& str);

  // Appends an unsigned integer to the internal buffer.
  void AppendUnsignedInt(uint32_t value);

  // Appends a char16 to the internal buffer.
  void AppendChar16(base::char16 ch);

  // Returns the registry key that belongs to the given |level|.
  const std::string& GetKey(PolicyLevel level);

  std::string mandatory_key_;
  std::string recommended_key_;
  std::string buffer_;

  // Safety check that every StartEntry() is followed by EndEntry().
  bool entry_started_ = false;
};

// Sets the proper keys for writing user and device policy.
class PRegUserDevicePolicyWriter : public PRegPolicyWriter {
 public:
  PRegUserDevicePolicyWriter();
};

// Sets the proper keys for writing policy for extensions.
class PRegExtensionPolicyWriter : public PRegPolicyWriter {
 public:
  explicit PRegExtensionPolicyWriter(const std::string& extension_id);

  // Updates registry keys for subsequent Append* calls to use |extension_id|.
  void SetExtensionId(const std::string& extension_id);
};

}  // namespace policy

#endif  // AUTHPOLICY_POLICY_PREG_POLICY_WRITER_H_
