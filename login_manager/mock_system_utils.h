// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_
#define LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_

#include <stdint.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "login_manager/system_utils.h"

namespace login_manager {

class MockSystemUtils : public SystemUtils {
 public:
  MockSystemUtils();
  ~MockSystemUtils() override;

  MOCK_METHOD3(kill, int(pid_t pid, uid_t uid, int signal));
  MOCK_METHOD1(time, time_t(time_t*));  // NOLINT
  MOCK_METHOD0(fork, pid_t(void));
  MOCK_METHOD2(GetAppOutput,
               bool(const std::vector<std::string>&, std::string*));
  MOCK_METHOD0(GetDevModeState, DevModeState(void));
  MOCK_METHOD0(GetVmState, VmState(void));
  MOCK_METHOD2(ProcessGroupIsGone, bool(pid_t child_spec,
                                        base::TimeDelta timeout));
  MOCK_METHOD2(EnsureAndReturnSafeFileSize, bool(const base::FilePath& file,
                                                 int32_t* file_size_32));
  MOCK_METHOD1(Exists, bool(const base::FilePath& file));
  MOCK_METHOD1(DirectoryExists, bool(const base::FilePath& dir));
  MOCK_METHOD1(IsDirectoryEmpty, bool(const base::FilePath& dir));
  MOCK_METHOD1(CreateReadOnlyFileInTempDir, bool(base::FilePath* temp_file));
  MOCK_METHOD2(CreateTemporaryDirIn, bool(const base::FilePath& parent_dir,
                                          base::FilePath* out_dir));
  MOCK_METHOD1(CreateDir, bool(const base::FilePath& dir));
  MOCK_METHOD1(GetUniqueFilenameInWriteOnlyTempDir,
               bool(base::FilePath* temp_file_path));
  MOCK_METHOD1(RemoveDirTree, bool(const base::FilePath& dir));
  MOCK_METHOD1(RemoveFile, bool(const base::FilePath& filename));
  MOCK_METHOD2(RenameDir, bool(const base::FilePath& source,
                               const base::FilePath& target));
  MOCK_METHOD2(AtomicFileWrite, bool(const base::FilePath& filename,
                                     const std::string& data));
  MOCK_METHOD1(AmountOfFreeDiskSpace, int64_t(const base::FilePath& path));
  MOCK_METHOD2(GetGroupInfo, bool(const std::string& group_name,
                                  gid_t* out_gid));
  MOCK_METHOD3(ChangeOwner, bool(const base::FilePath& filename,
                                 pid_t pid,
                                 gid_t gid));
  MOCK_METHOD2(SetPosixFilePermissions, bool(const base::FilePath& filename,
                                             mode_t mode));
  MOCK_METHOD1(CreateServerHandle,
               ScopedPlatformHandle(const NamedPlatformHandle& named_handle));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSystemUtils);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_
