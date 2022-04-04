// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/helpers/modetest_helper_utils.h"

#include <algorithm>

#include <re2/re2.h>

namespace debugd {
namespace modetest_helper_utils {
namespace {
// Looks like "    1 EDID:"
constexpr char kEDIDPropertyRegex[] = R"(^\s+\d+ EDID:$)";
constexpr char kValueRegex[] = R"(^\s+value:$)";
// The EDID is printed as a hex dump over several lines, each line containing
// the contents of 16 bytes. The first 16 bytes are broken down as follows:
//   uint64_t fixed_pattern;      // Always 00 FF FF FF FF FF FF 00.
//   uint16_t manufacturer_id;    // Manufacturer ID, encoded as PNP IDs.
//   uint16_t product_code;       // Manufacturer product code, little-endian.
//   uint32_t serial_number;      // Serial number, little-endian.
// Source: https://en.wikipedia.org/wiki/EDID#EDID_1.3_data_format
//
// The subsequent regex looks for the fixed pattern followed by two 32-bit
// fields (manufacturer + product, serial number).
constexpr char kEDIDSerialRegex[] = R"(^\s+(00f{12}00[0-9a-f]{8}[0-9a-f]{8}))";
}  // namespace

EDIDFilter::EDIDFilter() : saw_edid_property_(false), saw_value_(false) {}

void EDIDFilter::ProcessLine(std::string& line) {
  if (!saw_edid_property_) {
    saw_edid_property_ = RE2::FullMatch(line, kEDIDPropertyRegex);
  } else if (!saw_value_) {
    saw_value_ = RE2::FullMatch(line, kValueRegex);
  } else {
    re2::StringPiece s;
    // The first line in the EDID blob value should have the serial number
    // which we want to filter out.
    if (RE2::PartialMatch(line, kEDIDSerialRegex, &s)) {
      // Find the end of this match in |line|.
      auto it = std::search(line.rbegin(), line.rend(), s.rbegin(), s.rend());
      if (it != line.rend()) {
        // Clear the serial number, which is the first 8 characters.
        std::fill_n(it, 8, '0');
      }
    }
    // Reset these since we don't want to look at anymore of the blob
    // after we've looked at the first line. If we failed to find a
    // valid EDID (i.e. the match fails) we want to reset the state machine
    // as well.
    saw_value_ = false;
    saw_edid_property_ = false;
  }
}
}  // namespace modetest_helper_utils
}  // namespace debugd
