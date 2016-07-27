// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef IMAGELOADER_MOCK_MOUNT_OPS_H_
#define IMAGELOADER_MOCK_MOUNT_OPS_H_

#include "loop_mounter.h"

#include <string>

#include "gmock/gmock.h"

namespace base {
void PrintTo(const base::FilePath& path, std::ostream* stream) {
  *stream << path.value();
}
}

namespace imageloader {

class MockLoopMounter : public LoopMounter {
 public:
  MockLoopMounter() {}
  MOCK_METHOD2(Mount, bool(const base::ScopedFD&, const base::FilePath&));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockLoopMounter);
};

}  // namespace imageloader

#endif  // IMAGELOADER_MOCK_MOUNT_OPS_H_
