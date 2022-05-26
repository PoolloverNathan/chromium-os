/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_TESTS_AUTO_FRAMING_TEST_FIXTURE_H_
#define CAMERA_FEATURES_AUTO_FRAMING_TESTS_AUTO_FRAMING_TEST_FIXTURE_H_

#include <memory>
#include <vector>

#include <base/test/task_environment.h>

#include "common/stream_manipulator.h"
#include "features/auto_framing/auto_framing_stream_manipulator.h"
#include "features/auto_framing/tests/test_image.h"

namespace cros::tests {

struct TestFrameInfo {
  base::TimeDelta duration;
  Rect<uint32_t> face_rect;
};

class AutoFramingTestFixture {
 public:
  // Loads a test image that contains one face.  Test frames will be cropped
  // from the image to generate random face positions.
  bool LoadTestImage(const base::FilePath& path);

  // Sets up auto-framing pipeline that crops a |full_size| input into a
  // |stream_size| output.  |input_frame_infos| describes the test video content
  // piecewisely.
  bool SetUp(const Size& full_size,
             const Size& stream_size,
             float frame_rate,
             std::vector<TestFrameInfo> input_frame_infos);

  // Runs one test frame on the pipeline.
  bool ProcessFrame(int64_t sensor_timestamp,
                    bool is_enabled,
                    bool* is_face_detected);

 private:
  ScopedBufferHandle CreateTestFrameWithFace(uint32_t width,
                                             uint32_t height,
                                             uint32_t format,
                                             uint32_t usage,
                                             const Rect<uint32_t>& face_rect);
  bool ProcessCaptureRequest();
  bool ProcessCaptureResult(int64_t sensor_timestamp, bool* is_face_detected);
  size_t GetFrameIndex(int64_t sensor_timestamp) const;

  base::test::SingleThreadTaskEnvironment task_environment_;

  std::optional<TestImage> test_image_one_face_;

  StreamManipulator::RuntimeOptions runtime_options_;
  android::CameraMetadata static_info_;
  camera3_stream_t output_stream_ = {};
  std::vector<camera3_stream_t*> output_streams_;
  const camera3_stream_t* input_stream_ = nullptr;
  std::vector<TestFrameInfo> input_frame_infos_;
  std::vector<ScopedBufferHandle> input_buffers_;
  ScopedBufferHandle output_buffer_;
  android::CameraMetadata result_metadata_;
  uint32_t frame_number_ = 0;
  std::unique_ptr<AutoFramingStreamManipulator>
      auto_framing_stream_manipulator_;
};

}  // namespace cros::tests

#endif  // CAMERA_FEATURES_AUTO_FRAMING_TESTS_AUTO_FRAMING_TEST_FIXTURE_H_
