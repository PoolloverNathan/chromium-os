/*
 * Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera3_callback_ops_delegate.h"

#include <inttypes.h>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/strings/stringprintf.h>

#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "hal_adapter/camera_device_adapter.h"
#include "hal_adapter/camera_trace_event.h"

namespace cros {

Camera3CallbackOpsDelegate::Camera3CallbackOpsDelegate(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : internal::MojoRemote<mojom::Camera3CallbackOps>(task_runner) {}

void Camera3CallbackOpsDelegate::ProcessCaptureResult(
    mojom::Camera3CaptureResultPtr result) {
  VLOGF_ENTER();
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&Camera3CallbackOpsDelegate::ProcessCaptureResultOnThread,
                     base::AsWeakPtr(this), std::move(result)));
}

void Camera3CallbackOpsDelegate::Notify(mojom::Camera3NotifyMsgPtr msg) {
  VLOGF_ENTER();
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&Camera3CallbackOpsDelegate::NotifyOnThread,
                                base::AsWeakPtr(this), std::move(msg)));
}

void Camera3CallbackOpsDelegate::ProcessCaptureResultOnThread(
    mojom::Camera3CaptureResultPtr result) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_HAL_ADAPTER(kCameraTraceKeyFrameNumber, result->frame_number);

  // process_capture_result may be called multiple times for a single frame,
  // each time with a new disjoint piece of metadata and/or set of gralloc
  // buffers. The framework will accumulate these partial metadata results into
  // one result.
  // ref:
  // https://android.googlesource.com/platform/hardware/libhardware/+/8a6fed0d280014d84fe0f6a802f1cf29600e5bae/include/hardware/camera3.h#284
  if (result->output_buffers) {
    for (const auto& output_buffer : *result->output_buffers) {
      TRACE_HAL_ADAPTER_END(GetTraceTrack(HalAdapterTraceEvent::kCapture,
                                          result->frame_number,
                                          output_buffer->stream_id));
    }
  }

  remote_->ProcessCaptureResult(std::move(result));
}

void Camera3CallbackOpsDelegate::NotifyOnThread(
    mojom::Camera3NotifyMsgPtr msg) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_HAL_ADAPTER();

  remote_->Notify(std::move(msg));
}

}  // end of namespace cros
