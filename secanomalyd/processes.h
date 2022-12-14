// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_PROCESSES_H_
#define SECANOMALYD_PROCESSES_H_

#include <sys/types.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/strings/string_piece.h>

#include <brillo/process/process.h>

namespace secanomalyd {

class ProcEntry {
 public:
  ProcEntry() : pid_{}, pidns_{}, comm_{}, args_{} {}

  explicit ProcEntry(base::StringPiece proc_str);

  // Copying the private fields is fine.
  ProcEntry(const ProcEntry& other) = default;
  ProcEntry& operator=(const ProcEntry& other) = default;

  // TODO(jorgelo): Implement move constructor so that we can use emplace().

  pid_t pid() const { return pid_; }
  const ino_t pidns() const { return pidns_; }
  const std::string& comm() const { return comm_; }
  const std::string& args() const { return args_; }

 private:
  pid_t pid_;
  ino_t pidns_;
  std::string comm_;
  std::string args_;
};

using ProcEntries = std::vector<ProcEntry>;
using MaybeProcEntries = std::optional<ProcEntries>;

enum class ProcessFilter { kAll = 0, kInitPidNamespaceOnly };

MaybeProcEntries ReadProcesses(ProcessFilter filter);
// Used mostly for testing.
// |reader| is un-owned.
MaybeProcEntries ReadProcesses(brillo::Process* reader, ProcessFilter filter);
MaybeProcEntries ReadProcessesFromString(const std::string& procs,
                                         ProcessFilter filter);

}  // namespace secanomalyd

#endif  // SECANOMALYD_PROCESSES_H_
