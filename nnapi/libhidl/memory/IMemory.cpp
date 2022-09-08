// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IMemory HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++ -r android.hidl:system/libhidl/transport \
//   android.hidl.memory@1.0

#include <android/hidl/memory/1.0/IMemory.h>

namespace android {
namespace hidl {
namespace memory {
namespace V1_0 {

using ::android::sp;
using ::android::hardware::hidl_death_recipient;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

const char* IMemory::descriptor("android.hidl.memory@1.0::IMemory");

// Methods from ::android::hidl::base::V1_0::IBase follow.
Return<void> IMemory::interfaceChain(interfaceChain_cb _hidl_cb) {
  _hidl_cb({
      ::android::hidl::memory::V1_0::IMemory::descriptor,
      ::android::hidl::base::V1_0::IBase::descriptor,
  });
  return Void();
}

Return<void> IMemory::debug(const hidl_handle&, const hidl_vec<hidl_string>&) {
  return Void();
}

Return<void> IMemory::interfaceDescriptor(interfaceDescriptor_cb _hidl_cb) {
  _hidl_cb(::android::hidl::memory::V1_0::IMemory::descriptor);
  return Void();
}

Return<void> IMemory::getHashChain(getHashChain_cb _hidl_cb) {
  _hidl_cb(
      {/* 4632246017013e75536fa6ee47db286b24a323fb92c37c6b14bb0ab796b7a16b */
       (uint8_t[32]){70,  50,  36,  96,  23,  1,   62,  117, 83,  111, 166,
                     238, 71,  219, 40,  107, 36,  163, 35,  251, 146, 195,
                     124, 107, 20,  187, 10,  183, 150, 183, 161, 107},
       /* ec7fd79ed02dfa85bc499426adae3ebe23ef0524f3cd6957139324b83b18ca4c */
       (uint8_t[32]){236, 127, 215, 158, 208, 45,  250, 133, 188, 73,  148,
                     38,  173, 174, 62,  190, 35,  239, 5,   36,  243, 205,
                     105, 87,  19,  147, 36,  184, 59,  24,  202, 76}});
  return Void();
}

Return<void> IMemory::setHALInstrumentation() {
  return Void();
}

Return<bool> IMemory::linkToDeath(const sp<hidl_death_recipient>& recipient,
                                  uint64_t) {
  return (recipient != nullptr);
}

Return<void> IMemory::ping() {
  return Void();
}

Return<void> IMemory::getDebugInfo(getDebugInfo_cb _hidl_cb) {
  ::android::hidl::base::V1_0::DebugInfo info = {};
  info.pid = -1;
  info.ptr = 0;
  info.arch =
#if defined(__LP64__)
      ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_64BIT;
#else
      ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_32BIT;
#endif

  _hidl_cb(info);
  return Void();
}

Return<void> IMemory::notifySyspropsChanged() {
  ::android::report_sysprop_change();
  return Void();
}

Return<bool> IMemory::unlinkToDeath(const sp<hidl_death_recipient>& recipient) {
  return (recipient != nullptr);
}

Return<sp<IMemory>> IMemory::castFrom(const sp<IMemory>& parent, bool) {
  return parent;
}

}  // namespace V1_0
}  // namespace memory
}  // namespace hidl
}  // namespace android
