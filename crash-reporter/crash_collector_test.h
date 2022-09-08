// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_COLLECTOR_TEST_H_
#define CRASH_REPORTER_CRASH_COLLECTOR_TEST_H_

#include "crash-reporter/crash_collector.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class CrashCollectorMock : public CrashCollector {
 public:
  CrashCollectorMock();
  CrashCollectorMock(
      CrashDirectorySelectionMethod crash_directory_selection_method,
      CrashSendingMode crash_sending_mode);
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

#endif  // CRASH_REPORTER_CRASH_COLLECTOR_TEST_H_
