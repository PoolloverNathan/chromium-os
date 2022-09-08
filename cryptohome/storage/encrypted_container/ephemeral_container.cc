// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ephemeral_container.h"

#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <base/callback_helpers.h>
#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/encrypted_container/ramdisk_device.h"

namespace cryptohome {

EphemeralContainer::EphemeralContainer(
    std::unique_ptr<RamdiskDevice> backing_device, Platform* platform)
    : backing_device_(std::move(backing_device)), platform_(platform) {}

EphemeralContainer::EphemeralContainer(
    std::unique_ptr<FakeBackingDevice> backing_device, Platform* platform)
    : backing_device_(std::move(backing_device)), platform_(platform) {}

EphemeralContainer::~EphemeralContainer() {
  std::ignore = Teardown();
  std::ignore = Purge();
}

bool EphemeralContainer::Exists() {
  return backing_device_->Exists();
}

bool EphemeralContainer::Purge() {
  return backing_device_->Purge();
}

bool EphemeralContainer::Setup(const FileSystemKey& encryption_key) {
  // This is a validity check. Higher level code shouldn't even try using an
  // ephemeral container with keys, or try to re-use an existing one.
  if (encryption_key != FileSystemKey()) {
    LOG(ERROR) << "Encryption key for ephemeral must be empty";
    return false;
  }

  base::ScopedClosureRunner cleanup(base::BindOnce(
      [](EphemeralContainer* container, BackingDevice* backing_device) {
        // Try purging backing device even if teardown failed.
        std::ignore = container->Teardown();
        std::ignore = container->Purge();
      },
      base::Unretained(this), base::Unretained(backing_device_.get())));

  // Clean any pre-existing ram disks for the user.
  if (backing_device_->Exists()) {
    std::ignore = backing_device_->Teardown();
    if (!backing_device_->Purge()) {
      LOG(ERROR) << "Can't teardown previous backing store for the ephemeral.";
    }
  }

  // Create and setup the backing device the backing device.
  if (!backing_device_->Create()) {
    LOG(ERROR) << "Can't create backing store for the mount.";
    return false;
  }
  if (!backing_device_->Setup()) {
    LOG(ERROR) << "Can't setup backing store for the mount.";
    return false;
  }

  // Format the device. At this point, even if the backing device was already
  // present, it will lose all of its content.
  std::optional<base::FilePath> backing_device_path =
      backing_device_->GetPath();
  if (!backing_device_path.has_value()) {
    LOG(ERROR) << "Failed to get backing device path";
    return false;
  }
  if (!platform_->FormatExt4(*backing_device_path, kDefaultExt4FormatOpts, 0)) {
    LOG(ERROR) << "Can't format ephemeral backing device as ext4";
    return false;
  }

  std::ignore = cleanup.Release();

  return true;
}

bool EphemeralContainer::Reset() {
  // Reset should never be called for ephemeral containers.
  LOG(ERROR) << "Reset not supported on ephemeral containers";
  return false;
}

bool EphemeralContainer::Teardown() {
  // Try purging backing device even if teardown failed.
  std::ignore = backing_device_->Teardown();
  return backing_device_->Purge();
}

base::FilePath EphemeralContainer::GetBackingLocation() const {
  if (backing_device_ != nullptr && backing_device_->GetPath().has_value()) {
    return *(backing_device_->GetPath());
  }
  return base::FilePath();
}

}  // namespace cryptohome
