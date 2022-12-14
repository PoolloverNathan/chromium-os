// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crc32.h"

#include <vector>

#include <gtest/gtest.h>

namespace cryptohome {

// TODO(b/168049518): Move to libchrome.
TEST(Crc32, TestVector1) {
  const std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00};
  uint32_t sum = Crc32(data.data(), data.size());
  EXPECT_EQ(sum, 0x6522DF69);
}

TEST(Crc32, TestVector2) {
  const std::vector<uint8_t> data = {0xFF, 0xFF, 0xFF, 0xFF,
                                     0xFF, 0xFF, 0xFF, 0xFF};
  uint32_t sum = Crc32(data.data(), data.size());
  EXPECT_EQ(sum, 0x2144DF1C);
}

TEST(Crc32, TestVector3) {
  const std::vector<uint8_t> data = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
  uint32_t sum = Crc32(data.data(), data.size());
  EXPECT_EQ(sum, 0x91267E8A);
}

TEST(Crc32, TestVector4) {
  const std::vector<uint8_t> data = {
      0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15,
      0x14, 0x13, 0x12, 0x11, 0x10, 0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A,
      0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
  uint32_t sum = Crc32(data.data(), data.size());
  EXPECT_EQ(sum, 0x9AB0EF72);
}

}  // namespace cryptohome
