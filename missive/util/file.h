// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utilities for file operations.

#ifndef MISSIVE_UTIL_FILE_H_
#define MISSIVE_UTIL_FILE_H_

#include <string>

#include <base/callback.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/strings/strcat.h>

#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

// Deletes the given path, whether it's a file or a directory.
// This function is identical to base::DeleteFile() except that it issues a
// warning if the deletion fails. Useful when we do not care about whether the
// deletion succeeds or not.
bool DeleteFileWarnIfFailed(const base::FilePath& path);

// Enumerates over |dir_enum|, and deletes the file if pred.Run(file) returns
// true. If |pred| is unspecified, all files enumerated are deleted. It deletes
// each individual file with DeleteFileWarnIfFailed(). Refer to
// DeleteFileWarnIfFailed() for the effect of the deletion.
// Returns true if all files are deleted successfully, otherwise returns false.
bool DeleteFilesWarnIfFailed(
    base::FileEnumerator& dir_enum,
    base::RepeatingCallback<bool(const base::FilePath&)> pred =
        base::BindRepeating([](const base::FilePath&) { return true; }));

// Attempt to read entire file given from |file_path| starting from offset.
// Returns a string of the data read, or an error if one was encountered.
StatusOr<std::string> MaybeReadFile(const base::FilePath& file_path,
                                    int64_t offset);

// Appends |data| with a new line to |file_path|.
Status AppendLine(const base::FilePath& file_path,
                  const base::StringPiece& data);

// Overwrites or creates a new file at |file_path| with the contents |data|.
Status MaybeWriteFile(const base::FilePath& file_path,
                      const base::StringPiece& data);

// Removes the first |pos| bytes from a file at |file_path| and also removes
// the rest of the line which the byte at position |pos| was on.
StatusOr<uint32_t> RemoveAndTruncateLine(const base::FilePath& file_path,
                                         uint32_t pos);
}  // namespace reporting

#endif  // MISSIVE_UTIL_FILE_H_
