// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/make_stat.h"

#include <unistd.h>

#include <base/check.h>
#include <base/numerics/safe_conversions.h>
#include <base/time/time.h>

namespace fusebox {

bool IsAllowedStatMode(mode_t mode, mode_t allowed) {
  return mode & allowed;
}

mode_t MakeStatModeBits(mode_t mode, bool read_only) {
  CHECK(IsAllowedStatMode(mode));

  // Set read-only user bits.
  if (read_only)
    mode &= ~S_IWUSR;

  // Setup user execute bits.
  mode &= ~S_IXUSR;
  if (S_ISDIR(mode))
    mode |= S_IXUSR;

  // Dup user bits in group bits.
  mode &= ~S_IRWXG;
  if (mode & S_IRUSR)
    mode |= S_IRGRP;
  if (mode & S_IWUSR)
    mode |= S_IWGRP;
  if (mode & S_IXUSR)
    mode |= S_IXGRP;

  // Clear other permission bits.
  mode &= ~S_IRWXO;

  return mode;
}

struct stat MakeStat(ino_t ino, const struct stat& s, bool read_only) {
  CHECK(IsAllowedStatMode(s.st_mode));

  struct stat stat = s;
  stat.st_ino = ino;
  stat.st_mode = MakeStatModeBits(s.st_mode, read_only);
  stat.st_nlink = 1;
  stat.st_uid = getuid();
  stat.st_gid = getgid();

  return stat;
}

struct stat GetServerStat(ino_t ino,
                          dbus::MessageReader* reader,
                          bool read_only) {
  DCHECK(reader);

  int32_t mode = 0;
  CHECK(reader->PopInt32(&mode));
  int64_t size = 0;
  CHECK(reader->PopInt64(&size));
  double last_accessed = 0;
  CHECK(reader->PopDouble(&last_accessed));
  double last_modified = 0;
  CHECK(reader->PopDouble(&last_modified));
  double creation_time = 0;
  CHECK(reader->PopDouble(&creation_time));

  struct stat stat = {0};
  stat.st_ino = ino;
  stat.st_mode = MakeStatModeBits(mode | 0777, read_only);
  stat.st_size = size;
  stat.st_nlink = 1;
  stat.st_uid = getuid();
  stat.st_gid = getgid();

  using atime_type = decltype(stat.st_atime);
  struct timeval atime = base::Time::FromDoubleT(last_accessed).ToTimeVal();
  stat.st_atime = base::saturated_cast<atime_type>(atime.tv_sec);

  using mtime_type = decltype(stat.st_mtime);
  struct timeval mtime = base::Time::FromDoubleT(last_modified).ToTimeVal();
  stat.st_mtime = base::saturated_cast<mtime_type>(mtime.tv_sec);

  using ctime_type = decltype(stat.st_ctime);
  struct timeval ctime = base::Time::FromDoubleT(creation_time).ToTimeVal();
  stat.st_ctime = base::saturated_cast<ctime_type>(ctime.tv_sec);

  DCHECK(IsAllowedStatMode(stat.st_mode));
  return stat;
}

std::string StatModeToString(mode_t mode) {
  std::string mode_string("?");

  if (S_ISSOCK(mode))
    mode_string.at(0) = 's';
  else if (S_ISLNK(mode))
    mode_string.at(0) = 'l';
  else if (S_ISFIFO(mode))
    mode_string.at(0) = 'p';
  else if (S_ISBLK(mode))
    mode_string.at(0) = 'b';
  else if (S_ISCHR(mode))
    mode_string.at(0) = 'c';
  else if (S_ISDIR(mode))
    mode_string.at(0) = 'd';
  else if (S_ISREG(mode))
    mode_string.at(0) = '-';

  mode_string.append((mode & S_IRUSR) ? "r" : "-");
  mode_string.append((mode & S_IWUSR) ? "w" : "-");
  mode_string.append((mode & S_IXUSR) ? "x" : "-");
  mode_string.append((mode & S_IRGRP) ? "r" : "-");
  mode_string.append((mode & S_IWGRP) ? "w" : "-");
  mode_string.append((mode & S_IXGRP) ? "x" : "-");
  mode_string.append((mode & S_IROTH) ? "r" : "-");
  mode_string.append((mode & S_IWOTH) ? "w" : "-");
  mode_string.append((mode & S_IXOTH) ? "x" : "-");

  return mode_string;
}

}  // namespace fusebox
