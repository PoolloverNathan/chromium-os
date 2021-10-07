// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_UTIL_GET_RANDOM_SUFFIX_H_
#define CRYPTOHOME_UTIL_GET_RANDOM_SUFFIX_H_

#include <string>

namespace cryptohome {

// Creates a random string suitable to append to a filename. Returns an empty
// string in case of error.
std::string GetRandomSuffix();

}  // namespace cryptohome

#endif  // CRYPTOHOME_UTIL_GET_RANDOM_SUFFIX_H_
