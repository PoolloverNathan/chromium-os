// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_collector_test.h"

#include <unistd.h>
#include <cinttypes>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using base::FilePath;
using base::StringPrintf;
using brillo::FindLog;
using brillo::GetLog;

namespace {

const int kMaxEfiParts = 100;

}  // namespace

class KernelCollectorTest : public ::testing::Test {
 protected:
  void SetUpSuccessfulCollect();
  void SetUpSuccessfulWatchdog(const FilePath&);
  void WatchdogOptedOutHelper(const FilePath&);
  void WatchdogOKHelper(const FilePath&);
  void WatchdogOnlyLastBootHelper(const FilePath&);

  const FilePath& console_ramoops_file() const { return test_console_ramoops_; }
  const FilePath& console_ramoops_file_old() const {
    return test_console_ramoops_old_;
  }
  const FilePath& eventlog_file() const { return test_eventlog_; }
  const FilePath& bios_log_file() const { return test_bios_log_; }
  const FilePath& kcrash_file() const { return test_kcrash_; }
  const FilePath& efikcrash_file(int part) const {
    return test_efikcrash_[part];
  }
  const FilePath& test_crash_directory() const { return test_crash_directory_; }

  KernelCollectorMock collector_;

 private:
  void SetUp() override {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(false);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_kcrash_ = scoped_temp_dir_.GetPath().Append("kcrash");
    ASSERT_TRUE(base::CreateDirectory(test_kcrash_));
    collector_.OverridePreservedDumpPath(test_kcrash_);

    test_console_ramoops_ = test_kcrash_.Append("console-ramoops-0");
    ASSERT_FALSE(base::PathExists(test_console_ramoops_));
    test_console_ramoops_old_ = test_kcrash_.Append("console-ramoops");
    ASSERT_FALSE(base::PathExists(test_console_ramoops_old_));
    for (int i = 0; i < kMaxEfiParts; i++) {
      test_efikcrash_[i] = test_kcrash_.Append(
          StringPrintf("dmesg-efi-%" PRIu64,
                       (9876543210 * KernelCollector::EfiCrash::kMaxPart + i) *
                               KernelCollector::EfiCrash::kMaxDumpRecord +
                           1));
      ASSERT_FALSE(base::PathExists(test_efikcrash_[i]));
    }
    test_kcrash_ = test_kcrash_.Append("dmesg-ramoops-0");
    ASSERT_FALSE(base::PathExists(test_kcrash_));

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append("crash_directory");
    ASSERT_TRUE(base::CreateDirectory(test_crash_directory_));

    test_eventlog_ = scoped_temp_dir_.GetPath().Append("eventlog.txt");
    ASSERT_FALSE(base::PathExists(test_eventlog_));
    collector_.OverrideEventLogPath(test_eventlog_);

    test_bios_log_ = scoped_temp_dir_.GetPath().Append("bios_log");
    ASSERT_FALSE(base::PathExists(test_bios_log_));
    collector_.OverrideBiosLogPath(test_bios_log_);
    brillo::ClearLog();
  }

  FilePath test_console_ramoops_;
  FilePath test_console_ramoops_old_;
  FilePath test_eventlog_;
  FilePath test_bios_log_;
  FilePath test_kcrash_;
  FilePath test_efikcrash_[kMaxEfiParts];
  FilePath test_crash_directory_;
  base::ScopedTempDir scoped_temp_dir_;
};

TEST_F(KernelCollectorTest, ParseEfiCrashId) {
  uint64_t test_efi_crash_id = 150989600314002;
  EXPECT_EQ(1509896003,
            KernelCollector::EfiCrash::GetTimestamp(test_efi_crash_id));
  EXPECT_EQ(14, KernelCollector::EfiCrash::GetPart(test_efi_crash_id));
  EXPECT_EQ(2, KernelCollector::EfiCrash::GetCrashCount(test_efi_crash_id));
  EXPECT_EQ(test_efi_crash_id,
            KernelCollector::EfiCrash::GenerateId(1509896003, 14, 2));
}

TEST_F(KernelCollectorTest, GetEfiCrashType) {
  ASSERT_FALSE(base::PathExists(efikcrash_file(1)));
  std::string type;
  uint64_t test_efi_crash_id;
  sscanf(efikcrash_file(1).BaseName().value().c_str(), "%*10s%" PRIu64,
         &test_efi_crash_id);
  // Write header.
  ASSERT_TRUE(test_util::CreateFile(efikcrash_file(1), "Panic#1 Part#20"));
  KernelCollector::EfiCrash efi_crash(test_efi_crash_id, collector_);
  ASSERT_TRUE(efi_crash.GetType(&type));
  EXPECT_EQ("Panic", type);
}

TEST_F(KernelCollectorTest, LoadEfiCrash) {
  int efi_part_count = kMaxEfiParts - 1;
  std::string efi_part[kMaxEfiParts];
  std::string expected_dump;
  std::string dump;
  uint64_t test_efi_crash_id;
  sscanf(efikcrash_file(1).BaseName().value().c_str(), "%*10s%" PRIu64,
         &test_efi_crash_id);

  for (int i = 1; i <= efi_part_count; i++) {
    ASSERT_FALSE(base::PathExists(efikcrash_file(i)));
    efi_part[i] = StringPrintf("Panic#100 Part#%d\n", i);
    for (int j = 0; j < i; j++) {
      efi_part[i].append(StringPrintf("random blob %d\n", j));
    }
    ASSERT_TRUE(test_util::CreateFile(efikcrash_file(i), efi_part[i].c_str()));
  }
  KernelCollector::EfiCrash efi_crash(test_efi_crash_id, collector_);
  efi_crash.UpdateMaxPart(efi_crash.GetIdForPart(efi_part_count));
  ASSERT_TRUE(efi_crash.Load(&dump));

  // Stitch parts in reverse order.
  for (int i = efi_part_count; i > 0; i--) {
    // Strip first line since it contains header.
    expected_dump.append(efi_part[i], efi_part[i].find('\n') + 1,
                         std::string::npos);
  }
  EXPECT_EQ(expected_dump, dump);
}

TEST_F(KernelCollectorTest, ComputeKernelStackSignatureBase) {
  // Make sure the normal build architecture is detected
  EXPECT_NE(kernel_util::kArchUnknown, collector_.arch());
}

TEST_F(KernelCollectorTest, LoadPreservedDump) {
  ASSERT_FALSE(base::PathExists(kcrash_file()));
  std::string dump;
  dump.clear();

  ASSERT_TRUE(test_util::CreateFile(
      kcrash_file(), "CrashRecordWithoutRamoopsHeader\n<6>[    0.078852]"));
  ASSERT_TRUE(collector_.LoadParameters());
  ASSERT_TRUE(collector_.LoadPreservedDump(&dump));
  ASSERT_EQ("CrashRecordWithoutRamoopsHeader\n<6>[    0.078852]", dump);

  ASSERT_TRUE(test_util::CreateFile(kcrash_file(), "====1.1\nsomething"));
  ASSERT_TRUE(collector_.LoadParameters());
  ASSERT_TRUE(collector_.LoadPreservedDump(&dump));
  ASSERT_EQ("something", dump);

  ASSERT_TRUE(
      test_util::CreateFile(kcrash_file(), "\x01\x02\xfe\xff random blob"));
  ASSERT_TRUE(collector_.LoadParameters());
  ASSERT_FALSE(collector_.LoadPreservedDump(&dump));
  ASSERT_EQ("", dump);

  std::string large(1024 * 1024 + 1, 'x');  // 1MiB + 1 byte.
  ASSERT_TRUE(test_util::CreateFile(kcrash_file(), large));
  ASSERT_TRUE(collector_.LoadParameters());
  ASSERT_FALSE(collector_.LoadPreservedDump(&dump));
}

TEST_F(KernelCollectorTest, LoadBiosLog) {
  std::string dump;
  dump.clear();

  std::string bootblock_boot_1 =
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 bootblock starting"
      " (log level: 7)...\n"
      "This is boot 1 bootblock!\n"
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 verstage starting"
      " (log level: 7)...\n"
      "This is boot 1 verstage!\n";
  std::string romstage_boot_1 =
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 romstage starting...\n"
      "This is boot 1 romstage!\n"
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 ramstage starting...\n"
      "This is boot 1 ramstage!\n"
      "\n\nStarting depthcharge on kevin...\n"
      "This is boot 1 depthcharge!\n"
      "jumping to kernel\n"
      "Some more messages logged at runtime, maybe without terminating newline";
  std::string bootblock_boot_2 =
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 bootblock starting...\n"
      "This is boot 2 bootblock!\n"
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 verstage starting...\n"
      "This is boot 2 verstage!\n";
  std::string romstage_boot_2 =
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 romstage starting...\n"
      "This is boot 2 romstage!\n"
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 ramstage starting...\n"
      "This is boot 2 ramstage!\n"
      "\n\nStarting depthcharge on kevin...\n"
      "This is boot 2 depthcharge!\n"
      "jumping to kernel\n"
      "Some more messages logged at runtime, maybe without terminating newline";

  // Normal situation of multiple boots in log.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      ("Some old lines from boot N-3\n" +    // N-3
       bootblock_boot_2 + romstage_boot_2 +  // N-2
       bootblock_boot_1 + romstage_boot_1 +  // N-1 (the "last" boot)
       bootblock_boot_2 + romstage_boot_2)   // N ("current" boot, after crash)
          .c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_boot_1 + romstage_boot_1, "\n" + dump);

  // Same on a board that cannot log pre-romstage.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      (romstage_boot_2 + romstage_boot_1 + romstage_boot_2).c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(romstage_boot_1, "\n" + dump);

  // Logs from previous boot were lost.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(), (bootblock_boot_1 + romstage_boot_1).c_str()));
  ASSERT_FALSE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ("", dump);

  // No recognizable BIOS log.
  ASSERT_TRUE(test_util::CreateFile(bios_log_file(), "random crud\n"));
  ASSERT_FALSE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ("", dump);
}

TEST_F(KernelCollectorTest, EnableMissingKernel) {
  ASSERT_FALSE(collector_.Enable());
  ASSERT_FALSE(collector_.is_enabled());
  ASSERT_TRUE(FindLog("Kernel does not support crash dumping"));
}

TEST_F(KernelCollectorTest, EnableOK) {
  ASSERT_TRUE(test_util::CreateFile(kcrash_file(), ""));
  EXPECT_CALL(collector_, DumpDirMounted()).WillOnce(::testing::Return(true));
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(collector_.is_enabled());
  ASSERT_TRUE(FindLog("Enabling kernel crash handling"));
}

TEST_F(KernelCollectorTest, CollectPreservedFileMissing) {
  ASSERT_FALSE(collector_.Collect());
  ASSERT_FALSE(FindLog("Stored kcrash to "));
}

TEST_F(KernelCollectorTest, CollectBadDirectory) {
  ASSERT_TRUE(test_util::CreateFile(kcrash_file(), "====1.1\nsomething"));
  ASSERT_TRUE(collector_.Collect());
  ASSERT_TRUE(FindLog("Unable to create crash directory"))
      << "Did not find expected error string in log: {\n"
      << GetLog() << "}";
}

void KernelCollectorTest::SetUpSuccessfulCollect() {
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(test_util::CreateFile(kcrash_file(), "====1.1\nsomething"));
}

void KernelCollectorTest::SetUpSuccessfulWatchdog(const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(test_util::CreateFile(
      eventlog_file(),
      "112 | 2016-03-24 15:09:39 | System boot | 0\n"
      "113 | 2016-03-24 15:11:20 | System boot | 0\n"
      "114 | 2016-03-24 15:11:20 | Hardware watchdog reset\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

TEST_F(KernelCollectorTest, CollectOK) {
  SetUpSuccessfulCollect();
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      "BIOS Messages"
      "\n\ncoreboot-dc417eb Tue Nov 2 bootblock starting...\n"));
  ASSERT_TRUE(collector_.Collect());
  ASSERT_TRUE(FindLog("(handling)"));
  static const char kNamePrefix[] = "Stored kcrash to ";
  std::string log = brillo::GetLog();
  size_t pos = log.find(kNamePrefix);
  ASSERT_NE(std::string::npos, pos)
      << "Did not find string \"" << kNamePrefix << "\" in log: {\n"
      << log << "}";
  pos += strlen(kNamePrefix);
  std::string filename = log.substr(pos, std::string::npos);
  // Take the name up until \n
  size_t end_pos = filename.find_first_of("\n");
  ASSERT_NE(std::string::npos, end_pos);
  filename = filename.substr(0, end_pos);
  ASSERT_EQ(0, filename.find(test_crash_directory().value()));
  FilePath path(filename);
  ASSERT_TRUE(base::PathExists(path));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  ASSERT_EQ("something", contents);
  // Check that BIOS log was collected as well.
  path = path.ReplaceExtension("bios_log");
  ASSERT_TRUE(base::PathExists(path));
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  ASSERT_EQ("BIOS Messages", contents);
  // Confirm that files are correctly described in .meta file.
  path = path.ReplaceExtension("meta");
  ASSERT_TRUE(base::PathExists(path));
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  ASSERT_TRUE(
      contents.find("payload=" +
                    path.ReplaceExtension("kcrash").BaseName().value()) !=
      std::string::npos);
  ASSERT_TRUE(
      contents.find("upload_file_bios_log=" +
                    path.ReplaceExtension("bios_log").BaseName().value()) !=
      std::string::npos);
}

TEST_F(KernelCollectorTest, LastRebootWasNoCError) {
  const char kNoCError[] =
      "QTISECLIB [05699b8d7]GEM_NOC ERROR: ERRLOG0_LOW = 0x0000010d\n"
      "QTISECLIB [05699bbfd]SYSTEM_NOC ERROR: ERRLOG1_LOW = 0x00000045\n"
      "QTISECLIB [05699bb44]CONFIG_NOC ERROR: ERRLOG1_LOW = 0x00000063\n"
      "QTISECLIB [05699bcc3]GEM_NOC ERROR: ERRLOG0_HIGH = 0x00000003\n"
      "QTISECLIB [05699be20]CONFIG_NOC ERROR: ERRLOG1_HIGH = 0x00004626\n"
      "QTISECLIB [05699bd77]SYSTEM_NOC ERROR: ERRLOG1_HIGH = 0x00004626\n"
      "QTISECLIB [05699bf82]CONFIG_NOC ERROR: ERRLOG2_LOW = 0x00001000\n"
      "QTISECLIB [05699becc]GEM_NOC ERROR: ERRLOG1_LOW = 0x00000013\n"
      "QTISECLIB [05699c0eb]CONFIG_NOC ERROR: ERRLOG3_LOW = 0x00000028\n"
      "QTISECLIB [05699c036]SYSTEM_NOC ERROR: ERRLOG2_LOW = 0x00001000\n"
      "QTISECLIB [05699c259]CONFIG_NOC ERROR: SBM0 FAULTINSTATUS0_LOW = "
      "0x00000001\n"
      "QTISECLIB [05699c2f3]SYSTEM_NOC ERROR: ERRLOG3_LOW = 0x00000028\n"
      "QTISECLIB [05699c18a]GEM_NOC ERROR: ERRLOG1_HIGH = 0x00004626\n"
      "QTISECLIB [05699c48f]SYSTEM_NOC ERROR: SBM0 FAULTINSTATUS0_LOW = "
      "0x00000001\n"
      "QTISECLIB [05699c47b]NOC error fatal\n"
      "QTISECLIB [05699c515]GEM_NOC ERROR: ERRLOG2_LOW = 0x00001000\n"
      "QTISECLIB [05699c653]NOC error fatal\n"
      "QTISECLIB [05699c70f]GEM_NOC ERROR: ERRLOG3_LOW = 0x00000028\n"
      "QTISECLIB [05699c828]GEM_NOC ERROR: SBM0 FAULTINSTATUS0_LOW = "
      "0x00000001\n"
      "QTISECLIB [05699c97e]NOC error fatal\n"
      "QTISECLIB [05699d606]BL31 Error : NOC_ERROR\n"
      "QTISECLIB [0569b3675]SENDING NMI TO Q6 SUBSYSs!\n"
      "QTISECLIB [0569b367d]BL31 Error : NOC_ERROR\n"
      "QTISECLIB [0569e1c48]SENDING NMI TO Q6 SUBSYSs!\n"
      "QTISECLIB [0569e1c4e]BL31 Error : NOC_ERROR\n"
      "QTISECLIB [056a101bb]SENDING NMI TO Q6 SUBSYSs!\n"
      "";

  ASSERT_TRUE(collector_.LastRebootWasNoCError(kNoCError));
}

void KernelCollectorTest::WatchdogOKHelper(const FilePath& path) {
  SetUpSuccessfulWatchdog(path);
  ASSERT_TRUE(collector_.Collect());
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog("kernel-(WATCHDOG)-I can haz"));
}

TEST_F(KernelCollectorTest, BiosCrashArmOK) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  collector_.set_arch(kernel_util::kArchArm);
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      "PANIC in EL3 at x30 = 0x00003698"
      "\n\ncoreboot-dc417eb Tue Nov 2 bootblock starting...\n"));
  ASSERT_TRUE(collector_.Collect());
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog("bios-(PANIC)-0x00003698"));
}

TEST_F(KernelCollectorTest, WatchdogOK) {
  WatchdogOKHelper(console_ramoops_file());
}

TEST_F(KernelCollectorTest, WatchdogOKOld) {
  WatchdogOKHelper(console_ramoops_file_old());
}

void KernelCollectorTest::WatchdogOnlyLastBootHelper(const FilePath& path) {
  char next[] = "115 | 2016-03-24 15:24:27 | System boot | 0";
  SetUpSuccessfulWatchdog(path);
  ASSERT_TRUE(test_util::CreateFile(eventlog_file(), next));
  ASSERT_FALSE(collector_.Collect());
}

TEST_F(KernelCollectorTest, WatchdogOnlyLastBoot) {
  WatchdogOnlyLastBootHelper(console_ramoops_file());
}

TEST_F(KernelCollectorTest, WatchdogOnlyLastBootOld) {
  WatchdogOnlyLastBootHelper(console_ramoops_file_old());
}
