// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector.h"

#include <bits/wordsize.h>
#include <elf.h>
#include <unistd.h>

#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_split.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using base::FilePath;
using brillo::FindLog;

namespace {

int s_crashes = 0;
bool s_metrics = false;

const char kFilePath[] = "/my/path";

// Keep in sync with UserCollector::ShouldDump.
const char kChromeIgnoreMsg[] =
    "ignoring call by kernel - chrome crash; "
    "waiting for chrome to call us directly";

void CountCrash() {
  ++s_crashes;
}

bool IsMetrics() {
  return s_metrics;
}

}  // namespace

class UserCollectorMock : public UserCollector {
 public:
  MOCK_METHOD0(SetUpDBus, void());
};

class UserCollectorTest : public ::testing::Test {
  void SetUp() {
    s_crashes = 0;

    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    const pid_t pid = getpid();
    collector_.Initialize(CountCrash,
                          kFilePath,
                          IsMetrics,
                          false,
                          false,
                          false,
                          "",
                          [pid](pid_t p) { return p == pid + 1; });
    base::DeleteFile(FilePath("test"), true);
    mkdir("test", 0777);
    collector_.set_core_pattern_file("test/core_pattern");
    collector_.set_core_pipe_limit_file("test/core_pipe_limit");
    pid_ = pid;
    brillo::ClearLog();
  }

 protected:
  void ExpectFileEquals(const char *golden,
                        const FilePath &file_path) {
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(file_path, &contents));
    EXPECT_EQ(golden, contents);
  }

  std::vector<std::string> SplitLines(const std::string &lines) const {
    return base::SplitString(lines, "\n", base::KEEP_WHITESPACE,
                             base::SPLIT_WANT_ALL);
  }

  UserCollectorMock collector_;
  pid_t pid_;
};

TEST_F(UserCollectorTest, EnableOK) {
  ASSERT_TRUE(collector_.Enable());
  ExpectFileEquals("|/my/path --user=%P:%s:%u:%e",
                   FilePath("test/core_pattern"));
  ExpectFileEquals("4", FilePath("test/core_pipe_limit"));
  ASSERT_EQ(s_crashes, 0);
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
}

TEST_F(UserCollectorTest, EnableNoPatternFileAccess) {
  collector_.set_core_pattern_file("/does_not_exist");
  ASSERT_FALSE(collector_.Enable());
  ASSERT_EQ(s_crashes, 0);
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, EnableNoPipeLimitFileAccess) {
  collector_.set_core_pipe_limit_file("/does_not_exist");
  ASSERT_FALSE(collector_.Enable());
  ASSERT_EQ(s_crashes, 0);
  // Core pattern should not be written if we cannot access the pipe limit
  // or otherwise we may set a pattern that results in infinite recursion.
  ASSERT_FALSE(base::PathExists(FilePath("test/core_pattern")));
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, DisableOK) {
  ASSERT_TRUE(collector_.Disable());
  ExpectFileEquals("core", FilePath("test/core_pattern"));
  ASSERT_EQ(s_crashes, 0);
  EXPECT_TRUE(FindLog("Disabling user crash handling"));
}

TEST_F(UserCollectorTest, DisableNoFileAccess) {
  collector_.set_core_pattern_file("/does_not_exist");
  ASSERT_FALSE(collector_.Disable());
  ASSERT_EQ(s_crashes, 0);
  EXPECT_TRUE(FindLog("Disabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, ParseCrashAttributes) {
  pid_t pid;
  int signal;
  uid_t uid;
  std::string exec_name;
  EXPECT_TRUE(collector_.ParseCrashAttributes("123456:11:1000:foobar",
      &pid, &signal, &uid, &exec_name));
  EXPECT_EQ(123456, pid);
  EXPECT_EQ(11, signal);
  EXPECT_EQ(1000, uid);
  EXPECT_EQ("foobar", exec_name);
  EXPECT_TRUE(collector_.ParseCrashAttributes("4321:6:barfoo",
      &pid, &signal, &uid, &exec_name));
  EXPECT_EQ(4321, pid);
  EXPECT_EQ(6, signal);
  EXPECT_EQ(-1, uid);
  EXPECT_EQ("barfoo", exec_name);

  EXPECT_FALSE(collector_.ParseCrashAttributes("123456:11",
      &pid, &signal, &uid, &exec_name));

  EXPECT_TRUE(collector_.ParseCrashAttributes("123456:11:exec:extra",
      &pid, &signal, &uid, &exec_name));
  EXPECT_EQ("exec:extra", exec_name);

  EXPECT_FALSE(collector_.ParseCrashAttributes("12345p:11:foobar",
      &pid, &signal, &uid, &exec_name));

  EXPECT_FALSE(collector_.ParseCrashAttributes("123456:1 :foobar",
      &pid, &signal, &uid, &exec_name));

  EXPECT_FALSE(collector_.ParseCrashAttributes("123456::foobar",
      &pid, &signal, &uid, &exec_name));
}

TEST_F(UserCollectorTest, ShouldDumpFiltering) {
  std::string reason;
  EXPECT_FALSE(collector_.ShouldDump(pid_ + 1, true, false, false,
                                     "chrome-wm", &reason));
  EXPECT_EQ("ignoring - PID filtered out", reason);
}

TEST_F(UserCollectorTest, ShouldDumpDeveloperImageOverridesConsent) {
  std::string reason;
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, false,
                                    "chrome-wm", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);

  // When running a crash test, behave as normal.
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "chrome-wm", &reason));
  EXPECT_EQ("ignoring - no consent", reason);
}

TEST_F(UserCollectorTest, ShouldDumpChromeOverridesDeveloperImage) {
  std::string reason;
  // When running a crash test, behave as normal.
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "chrome", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_Compositor", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_PipelineThread", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "Chrome_ChildIOThread", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_Chrome_ChildIOT", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_ChromotingClien", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_LocalInputMonit", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);

  // When running a developer image, test that chrome crashes are handled
  // when the "handle_chrome_crashes" flag is set.
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "chrome", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_Compositor", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_PipelineThread", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "Chrome_ChildIOThread", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_Chrome_ChildIOT", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_ChromotingClien", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_LocalInputMonit", &reason));
  EXPECT_EQ("developer build - not testing - always dumping",
            reason);
}

TEST_F(UserCollectorTest, ShouldDumpUserConsentProductionImage) {
  std::string reason;
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "chrome-wm", &reason));
  EXPECT_EQ("ignoring - no consent", reason);

  EXPECT_TRUE(collector_.ShouldDump(pid_, true, false, false,
                                    "chrome-wm", &reason));
  EXPECT_EQ("handling", reason);
}

TEST_F(UserCollectorTest, HandleCrashWithoutConsent) {
  s_metrics = false;
  collector_.HandleCrash("20:10:ignored", "foobar");
  EXPECT_TRUE(FindLog(
      "Received crash notification for foobar[20] sig 10"));
  ASSERT_EQ(s_crashes, 0);
}

TEST_F(UserCollectorTest, HandleNonChromeCrashWithConsent) {
  s_metrics = true;
  collector_.HandleCrash("5:2:ignored", "chromeos-wm");
  EXPECT_TRUE(FindLog(
      "Received crash notification for chromeos-wm[5] sig 2"));
  ASSERT_EQ(s_crashes, 1);
}

TEST_F(UserCollectorTest, HandleChromeCrashWithConsent) {
  s_metrics = true;
  collector_.HandleCrash("5:2:ignored", "chrome");
  EXPECT_TRUE(FindLog(
      "Received crash notification for chrome[5] sig 2"));
  EXPECT_TRUE(FindLog(kChromeIgnoreMsg));
  ASSERT_EQ(s_crashes, 0);
}

TEST_F(UserCollectorTest, HandleSuppliedChromeCrashWithConsent) {
  s_metrics = true;
  collector_.HandleCrash("0:2:chrome", nullptr);
  EXPECT_TRUE(FindLog(
      "Received crash notification for supplied_chrome[0] sig 2"));
  EXPECT_TRUE(FindLog(kChromeIgnoreMsg));
  ASSERT_EQ(s_crashes, 0);
}

TEST_F(UserCollectorTest, GetProcessPath) {
  FilePath path = collector_.GetProcessPath(100);
  ASSERT_EQ("/proc/100", path.value());
}

TEST_F(UserCollectorTest, GetSymlinkTarget) {
  FilePath result;
  ASSERT_FALSE(collector_.GetSymlinkTarget(FilePath("/does_not_exist"),
                                           &result));
  ASSERT_TRUE(FindLog(
      "Readlink failed on /does_not_exist with 2"));
  std::string long_link;
  for (int i = 0; i < 50; ++i)
    long_link += "0123456789";
  long_link += "/gold";

  for (size_t len = 1; len <= long_link.size(); ++len) {
    std::string this_link;
    static const char kLink[] = "test/this_link";
    this_link.assign(long_link.c_str(), len);
    ASSERT_EQ(len, this_link.size());
    unlink(kLink);
    ASSERT_EQ(0, symlink(this_link.c_str(), kLink));
    ASSERT_TRUE(collector_.GetSymlinkTarget(FilePath(kLink), &result));
    ASSERT_EQ(this_link, result.value());
  }
}

TEST_F(UserCollectorTest, GetExecutableBaseNameFromPid) {
  std::string base_name;
  EXPECT_FALSE(collector_.GetExecutableBaseNameFromPid(0, &base_name));
  EXPECT_TRUE(FindLog(
      "Readlink failed on /proc/0/exe with 2"));
  EXPECT_TRUE(FindLog(
      "GetSymlinkTarget failed - Path /proc/0 DirectoryExists: 0"));
  EXPECT_TRUE(FindLog("stat /proc/0/exe failed: -1 2"));

  brillo::ClearLog();
  pid_t my_pid = getpid();
  EXPECT_TRUE(collector_.GetExecutableBaseNameFromPid(my_pid, &base_name));
  EXPECT_FALSE(FindLog("Readlink failed"));
  EXPECT_EQ("crash_reporter_test", base_name);
}

TEST_F(UserCollectorTest, GetFirstLineWithPrefix) {
  std::vector<std::string> lines;
  std::string line;

  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Name:", &line));
  EXPECT_EQ("", line);

  lines.push_back("Name:\tls");
  lines.push_back("State:\tR (running)");
  lines.push_back(" Foo:\t1000");

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, "Name:", &line));
  EXPECT_EQ(lines[0], line);

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, "State:", &line));
  EXPECT_EQ(lines[1], line);

  line.clear();
  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Foo:", &line));
  EXPECT_EQ("", line);

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, " Foo:", &line));
  EXPECT_EQ(lines[2], line);

  line.clear();
  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Bar:", &line));
  EXPECT_EQ("", line);
}

TEST_F(UserCollectorTest, GetIdFromStatus) {
  int id = 1;
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                          UserCollector::kIdEffective,
                                          SplitLines("nothing here"),
                                          &id));
  EXPECT_EQ(id, 1);

  // Not enough parameters.
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                          UserCollector::kIdReal,
                                          SplitLines("line 1\nUid:\t1\n"),
                                          &id));

  const std::vector<std::string> valid_contents =
      SplitLines("\nUid:\t1\t2\t3\t4\nGid:\t5\t6\t7\t8\n");
  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdReal,
                                         valid_contents,
                                         &id));
  EXPECT_EQ(1, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdEffective,
                                         valid_contents,
                                         &id));
  EXPECT_EQ(2, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdFileSystem,
                                         valid_contents,
                                         &id));
  EXPECT_EQ(4, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kGroupId,
                                         UserCollector::kIdEffective,
                                         valid_contents,
                                         &id));
  EXPECT_EQ(6, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kGroupId,
                                         UserCollector::kIdSet,
                                         valid_contents,
                                         &id));
  EXPECT_EQ(7, id);

  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kGroupId,
                                          UserCollector::IdKind(5),
                                          valid_contents,
                                          &id));
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kGroupId,
                                          UserCollector::IdKind(-1),
                                          valid_contents,
                                          &id));

  // Fail if junk after number
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                          UserCollector::kIdReal,
                                          SplitLines("Uid:\t1f\t2\t3\t4\n"),
                                          &id));
  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdReal,
                                         SplitLines("Uid:\t1\t2\t3\t4\n"),
                                         &id));
  EXPECT_EQ(1, id);

  // Fail if more than 4 numbers.
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                          UserCollector::kIdReal,
                                          SplitLines("Uid:\t1\t2\t3\t4\t5\n"),
                                          &id));
}

TEST_F(UserCollectorTest, GetStateFromStatus) {
  std::string state;
  EXPECT_FALSE(collector_.GetStateFromStatus(SplitLines("nothing here"),
                                             &state));
  EXPECT_EQ("", state);

  EXPECT_TRUE(collector_.GetStateFromStatus(SplitLines("State:\tR (running)"),
                                            &state));
  EXPECT_EQ("R (running)", state);

  EXPECT_TRUE(collector_.GetStateFromStatus(
      SplitLines("Name:\tls\nState:\tZ (zombie)\n"), &state));
  EXPECT_EQ("Z (zombie)", state);
}

TEST_F(UserCollectorTest, GetUserInfoFromName) {
  gid_t gid = 100;
  uid_t uid = 100;
  EXPECT_TRUE(collector_.GetUserInfoFromName("root", &uid, &gid));
  EXPECT_EQ(0, uid);
  EXPECT_EQ(0, gid);
}

TEST_F(UserCollectorTest, CopyOffProcFilesBadPath) {
  // Try a path that is not writable.
  ASSERT_FALSE(collector_.CopyOffProcFiles(pid_, FilePath("/bad/path")));
  EXPECT_TRUE(FindLog("Could not create /bad/path"));
}

TEST_F(UserCollectorTest, CopyOffProcFilesBadPid) {
  FilePath container_path("test/container");
  ASSERT_FALSE(collector_.CopyOffProcFiles(0, container_path));
  EXPECT_TRUE(FindLog("Path /proc/0 does not exist"));
}

TEST_F(UserCollectorTest, CopyOffProcFilesOK) {
  FilePath container_path("test/container");
  ASSERT_TRUE(collector_.CopyOffProcFiles(pid_, container_path));
  EXPECT_FALSE(FindLog("Could not copy"));
  static struct {
    const char *name;
    bool exists;
  } expectations[] = {
    { "auxv", true },
    { "cmdline", true },
    { "environ", true },
    { "maps", true },
    { "mem", false },
    { "mounts", false },
    { "sched", false },
    { "status", true }
  };
  for (unsigned i = 0; i < sizeof(expectations)/sizeof(expectations[0]); ++i) {
    EXPECT_EQ(expectations[i].exists,
              base::PathExists(
                  container_path.Append(expectations[i].name)));
  }
}

TEST_F(UserCollectorTest, ValidateProcFiles) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath container_dir = temp_dir.path();

  // maps file not exists (i.e. GetFileSize fails)
  EXPECT_FALSE(collector_.ValidateProcFiles(container_dir));

  // maps file is empty
  FilePath maps_file = container_dir.Append("maps");
  ASSERT_EQ(0, base::WriteFile(maps_file, nullptr, 0));
  ASSERT_TRUE(base::PathExists(maps_file));
  EXPECT_FALSE(collector_.ValidateProcFiles(container_dir));

  // maps file is not empty
  const char data[] = "test data";
  ASSERT_EQ(sizeof(data), base::WriteFile(maps_file, data, sizeof(data)));
  ASSERT_TRUE(base::PathExists(maps_file));
  EXPECT_TRUE(collector_.ValidateProcFiles(container_dir));
}

TEST_F(UserCollectorTest, ValidateCoreFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath container_dir = temp_dir.path();
  FilePath core_file = container_dir.Append("core");

  // Core file does not exist
  EXPECT_EQ(UserCollector::kErrorInvalidCoreFile,
            collector_.ValidateCoreFile(core_file));
  char e_ident[EI_NIDENT];
  e_ident[EI_MAG0] = ELFMAG0;
  e_ident[EI_MAG1] = ELFMAG1;
  e_ident[EI_MAG2] = ELFMAG2;
  e_ident[EI_MAG3] = ELFMAG3;
#if __WORDSIZE == 32
  e_ident[EI_CLASS] = ELFCLASS32;
#elif __WORDSIZE == 64
  e_ident[EI_CLASS] = ELFCLASS64;
#else
#error Unknown/unsupported value of __WORDSIZE.
#endif

  // Core file has the expected header
  ASSERT_TRUE(base::WriteFile(core_file, e_ident, sizeof(e_ident)));
  EXPECT_EQ(UserCollector::kErrorNone,
            collector_.ValidateCoreFile(core_file));

#if __WORDSIZE == 64
  // 32-bit core file on 64-bit platform
  e_ident[EI_CLASS] = ELFCLASS32;
  ASSERT_TRUE(base::WriteFile(core_file, e_ident, sizeof(e_ident)));
  EXPECT_EQ(UserCollector::kErrorUnsupported32BitCoreFile,
            collector_.ValidateCoreFile(core_file));
  e_ident[EI_CLASS] = ELFCLASS64;
#endif

  // Invalid core files
  ASSERT_TRUE(base::WriteFile(core_file, e_ident, sizeof(e_ident) - 1));
  EXPECT_EQ(UserCollector::kErrorInvalidCoreFile,
            collector_.ValidateCoreFile(core_file));

  e_ident[EI_MAG0] = 0;
  ASSERT_TRUE(base::WriteFile(core_file, e_ident, sizeof(e_ident)));
  EXPECT_EQ(UserCollector::kErrorInvalidCoreFile,
            collector_.ValidateCoreFile(core_file));
}
