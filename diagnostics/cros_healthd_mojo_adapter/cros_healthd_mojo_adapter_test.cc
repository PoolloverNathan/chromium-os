// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

namespace diagnostics {
namespace {

class CrosHealthdMojoAdapterImplTest : public testing::Test {
 protected:
  CrosHealthdMojoAdapterImplTest() = default;
  CrosHealthdMojoAdapterImplTest(const CrosHealthdMojoAdapterImplTest&) =
      delete;
  CrosHealthdMojoAdapterImplTest& operator=(
      const CrosHealthdMojoAdapterImplTest&) = delete;
};

}  // namespace
}  // namespace diagnostics
