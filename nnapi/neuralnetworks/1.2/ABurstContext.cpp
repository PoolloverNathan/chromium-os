// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IAllocator HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++-adapter -r android.hardware:hardware/interfaces \
//   android.hardware.neuralnetworks@1.2

#include <android/hardware/neuralnetworks/1.2/ABurstContext.h>
#include <android/hardware/neuralnetworks/1.2/IBurstContext.h>
#include <hidladapter/HidlBinderAdapter.h>

namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_2 {

ABurstContext::ABurstContext(
    const ::android::sp<
        ::android::hardware::neuralnetworks::V1_2::IBurstContext>& impl)
    : mImpl(impl) {
}  // Methods from ::android::hardware::neuralnetworks::V1_2::IBurstContext
   // follow.
::android::hardware::Return<void> ABurstContext::freeMemory(int32_t slot) {
  auto _hidl_out = mImpl->freeMemory(slot);
  return _hidl_out;
}

// Methods from ::android::hidl::base::V1_0::IBase follow.

}  // namespace V1_2
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android
