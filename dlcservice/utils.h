// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_UTILS_H_
#define DLCSERVICE_UTILS_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <libimageloader/manifest.h>

#include "dlcservice/boot/boot_slot.h"

namespace dlcservice {

extern char kDlcDirAName[];
extern char kDlcDirBName[];

// Important DLC file names.
extern char kDlcImageFileName[];
extern char kManifestName[];

// The directory inside a DLC module that contains all the DLC files.
extern char kRootDirectoryInsideDlcModule[];

// Permissions for DLC files and directories.
extern const int kDlcFilePerms;
extern const int kDlcDirectoryPerms;

template <typename Arg>
base::FilePath JoinPaths(Arg&& path) {
  return base::FilePath(path);
}

template <typename Arg, typename... Args>
base::FilePath JoinPaths(Arg&& path, Args&&... paths) {
  return base::FilePath(path).Append(JoinPaths(paths...));
}

// Splits the partition device name into the block device name and partition
// number. For example, "/dev/sda3" will be split into {"/dev/sda", 3} and
// "/dev/mmcblk0p2" into {"/dev/mmcblk0", 2}
// Returns false when malformed device name is passed in.
// If both output parameters are omitted (null), can be used
// just to test the validity of the device name. Note that the function
// simply checks if the device name looks like a valid device, no other
// checks are performed (i.e. it doesn't check if the device actually exists).
bool SplitPartitionName(std::string partition_name,
                        std::string* disk_name_out,
                        int* partition_num_out);

// Inverse of `SplitPartitionName`, will join arguments to produce a valid
// partition path.
// TODO(kimjae): Support ubifs format.
std::string JoinPartitionName(std::string device_name, int partition_num);

// Writes |data| into file |path|. Returns true if all |size| of |data| are
// written.
bool WriteToFile(const base::FilePath& path, const std::string& data);

// Same as |WriteToFile| but it does not alter the size of the file if the size
// of |data| is smaller than the size of the file on disk.
bool WriteToImage(const base::FilePath& path, const std::string& data);

// Creates a directory with permissions required for DLC modules.
bool CreateDir(const base::FilePath& path);

// Creates a directory with an empty file and resizes it.
bool CreateFile(const base::FilePath& path, int64_t size);

// Resizes the file in |path| to a new |size|. When shrinking, meaning current
// file size is > |size|, the file will only be resized and not unsparsed as the
// resized file is already assumed to be unsparse. When increasing, meaning
// current file size is <  |size|, the file will be resized and unsparsed only
// to the portions that increased from current file size to |size|. When neither
// shrinking nor increasing, nothing happens.
bool ResizeFile(const base::FilePath& path, int64_t size);

// Hashes the file at |path|.
bool HashFile(const base::FilePath& path,
              int64_t size,
              std::vector<uint8_t>* sha256);

// Copies and hashes the |from| file.
bool CopyAndHashFile(const base::FilePath& from,
                     const base::FilePath& to,
                     int64_t size,
                     std::vector<uint8_t>* sha256);

// Returns the path to a DLC module image given the |id| and |package|.
base::FilePath GetDlcImagePath(const base::FilePath& dlc_module_root_path,
                               const std::string& id,
                               const std::string& package,
                               BootSlot::Slot current_slot);

std::shared_ptr<imageloader::Manifest> GetDlcManifest(
    const base::FilePath& dlc_manifest_path,
    const std::string& id,
    const std::string& package);

// Scans a directory and returns all its subdirectory names in a list.
std::set<std::string> ScanDirectory(const base::FilePath& dir);

}  // namespace dlcservice

#endif  // DLCSERVICE_UTILS_H_
