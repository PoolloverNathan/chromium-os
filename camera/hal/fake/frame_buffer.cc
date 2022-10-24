/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/frame_buffer.h"

#include <sys/mman.h>

#include <utility>

#include <base/memory/ptr_util.h>
#include <hardware/gralloc.h>
#include <linux/videodev2.h>

#include "cros-camera/common.h"

namespace cros {

// static
std::unique_ptr<FrameBuffer> FrameBuffer::Wrap(buffer_handle_t buffer,
                                               uint32_t width,
                                               uint32_t height) {
  auto frame_buffer = base::WrapUnique(new FrameBuffer());
  if (!frame_buffer->Initialize(buffer, width, height)) {
    return nullptr;
  }
  return frame_buffer;
}

// static
std::unique_ptr<FrameBuffer> FrameBuffer::Create(
    uint32_t width, uint32_t height, android_pixel_format_t hal_format) {
  auto frame_buffer = base::WrapUnique(new FrameBuffer());
  if (!frame_buffer->Initialize(width, height, hal_format)) {
    return nullptr;
  }
  return frame_buffer;
}

FrameBuffer::FrameBuffer()
    : buffer_manager_(CameraBufferManager::GetInstance()) {}

bool FrameBuffer::Initialize(buffer_handle_t buffer,
                             uint32_t width,
                             uint32_t height) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int ret = buffer_manager_->Register(buffer);
  if (ret != 0) {
    LOGF(ERROR) << "Failed to register buffer";
    return false;
  }

  buffer_ = buffer;
  width_ = width;
  height_ = height;
  fourcc_ = buffer_manager_->GetV4L2PixelFormat(buffer_);
  if (fourcc_ == 0) {
    LOGF(ERROR) << "Failed to get V4L2 pixel format";
    return false;
  }
  uint32_t num_planes = buffer_manager_->GetNumPlanes(buffer_);
  if (num_planes == 0) {
    LOGF(ERROR) << "Failed to get number of planes";
    return false;
  }
  data_.resize(num_planes);
  stride_.resize(num_planes);

  return true;
}

bool FrameBuffer::Initialize(uint32_t width,
                             uint32_t height,
                             android_pixel_format_t hal_format) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  uint32_t hal_usage =
      GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

  uint32_t stride;
  int ret = buffer_manager_->Allocate(width, height, hal_format, hal_usage,
                                      &buffer_, &stride);
  if (ret) {
    LOGF(ERROR) << "Failed to allocate buffer";
    return false;
  }

  is_buffer_owned_ = true;
  width_ = width;
  height_ = height;
  fourcc_ = buffer_manager_->GetV4L2PixelFormat(buffer_);
  if (fourcc_ == 0) {
    LOGF(ERROR) << "Failed to get V4L2 pixel format";
    return false;
  }
  uint32_t num_planes = buffer_manager_->GetNumPlanes(buffer_);
  if (num_planes == 0) {
    LOGF(ERROR) << "Failed to get number of planes";
    return false;
  }
  data_.resize(num_planes);
  stride_.resize(num_planes);

  return true;
}

FrameBuffer::~FrameBuffer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (buffer_ == nullptr) {
    return;
  }

  if (!Unmap()) {
    LOGF(ERROR) << "Unmap failed";
  }

  if (is_buffer_owned_) {
    int ret = buffer_manager_->Free(buffer_);
    if (ret != 0) {
      LOGF(ERROR) << "Failed to free buffer";
    }
  } else {
    int ret = buffer_manager_->Deregister(buffer_);
    if (ret != 0) {
      LOGF(ERROR) << "Failed to unregister buffer";
    }
  }
}

bool FrameBuffer::Map() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_mapped_) {
    return true;
  }

  void* addr;
  int ret;
  switch (fourcc_) {
    case V4L2_PIX_FMT_JPEG:
      ret = buffer_manager_->Lock(buffer_, 0, 0, 0, 0, 0, &addr);
      if (ret != 0) {
        LOGF(ERROR) << "Failed to map buffer";
        return false;
      }
      data_[0] = static_cast<uint8_t*>(addr);
      break;
    case V4L2_PIX_FMT_NV12: {
      struct android_ycbcr ycbcr;
      ret = buffer_manager_->LockYCbCr(buffer_, 0, 0, 0, 0, 0, &ycbcr);
      if (ret != 0) {
        LOGF(ERROR) << "Failed to map buffer";
        return false;
      }
      data_[kYPlane] = static_cast<uint8_t*>(ycbcr.y);
      data_[kUPlane] = static_cast<uint8_t*>(ycbcr.cb);
      stride_[kYPlane] = ycbcr.ystride;
      stride_[kUPlane] = ycbcr.cstride;
      break;
    }
    case V4L2_PIX_FMT_YVU420: {
      struct android_ycbcr ycbcr;
      ret = buffer_manager_->LockYCbCr(buffer_, 0, 0, 0, 0, 0, &ycbcr);
      if (ret != 0) {
        LOGF(ERROR) << "Failed to map buffer";
        return false;
      }
      data_[kYPlane] = static_cast<uint8_t*>(ycbcr.y);
      data_[kUPlane] = static_cast<uint8_t*>(ycbcr.cb);
      data_[kVPlane] = static_cast<uint8_t*>(ycbcr.cr);
      stride_[kYPlane] = ycbcr.ystride;
      stride_[kUPlane] = ycbcr.cstride;
      stride_[kVPlane] = ycbcr.cstride;
      break;
    }
    default:
      LOGF(ERROR) << "Format " << FormatToString(fourcc_) << " is unsupported";
      return false;
  }

  is_mapped_ = true;
  return true;
}

bool FrameBuffer::Unmap() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_mapped_)
    return true;

  if (buffer_manager_->Unlock(buffer_) != 0) {
    LOGF(ERROR) << "Failed to unmap buffer";
    return false;
  }
  std::fill(data_.begin(), data_.end(), nullptr);
  std::fill(stride_.begin(), stride_.end(), 0);
  is_mapped_ = false;
  return true;
}

uint8_t* FrameBuffer::GetData(size_t plane) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_mapped_) {
    LOGF(ERROR) << "Buffer is not mapped";
    return nullptr;
  }
  if (plane >= data_.size()) {
    LOGF(ERROR) << "Invalid plane " << plane;
    return nullptr;
  }
  return data_[plane];
}

size_t FrameBuffer::GetStride(size_t plane) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!is_mapped_) {
    LOGF(ERROR) << "Buffer is not mapped";
    return 0;
  }
  if (plane >= stride_.size()) {
    LOGF(ERROR) << "Invalid plane " << plane;
    return 0;
  }
  return stride_[plane];
}

size_t FrameBuffer::GetPlaneSize(size_t plane) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return buffer_manager_->GetPlaneSize(buffer_, plane);
}

}  // namespace cros