// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_COMMON_TYPES_H_
#define IIOSERVICE_DAEMON_COMMON_TYPES_H_

#include <linux/iio/types.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <mojo/public/cpp/bindings/receiver_set.h>

#include <libmems/iio_device.h>

#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

enum Location {
  kNone = 0,
  kBase = 1,
  kLid = 2,
  kCamera = 3,
};

base::Optional<base::FilePath> GetAbsoluteSysPath(
    libmems::IioDevice* const iio_device);

class DeviceData {
 public:
  DeviceData(libmems::IioDevice* const iio_device = nullptr,
             std::set<cros::mojom::DeviceType> types = {});

  libmems::IioDevice* const iio_device;
  const std::set<cros::mojom::DeviceType> types;
  const bool on_dut;
};

class ClientData {
 public:
  explicit ClientData(const mojo::ReceiverId id,
                      DeviceData* device_data = nullptr);

  bool IsSampleActive() const;
  bool IsEventActive() const;

  const mojo::ReceiverId id;
  DeviceData* const device_data;

  std::set<int32_t> enabled_chn_indices;
  double frequency = -1;    // Hz
  uint32_t timeout = 5000;  // millisecond
  mojo::Remote<cros::mojom::SensorDeviceSamplesObserver> samples_observer;

  std::set<int32_t> enabled_event_indices;
  mojo::Remote<cros::mojom::SensorDeviceEventsObserver> events_observer;
};

struct SampleData {
  // The starting index of the next sample.
  uint64_t sample_index = 0;
  // Moving averages of channels except for channels that have no batch mode
  std::map<int32_t, int64_t> chns;
};

std::vector<std::string> GetGravityChannels();

constexpr char kInputAttr[] = "input";

// Number of axes for x, y, and z.
constexpr int kNumberOfAxes = 3;
constexpr char kChannelFormat[] = "%s_%c";
constexpr char kChannelAxes[kNumberOfAxes] = {'x', 'y', 'z'};

constexpr char kSamplingFrequencyAvailableFormat[] = "0.000000 %.6f %.6f";
std::string GetSamplingFrequencyAvailable(double min_frequency,
                                          double max_frequency);

base::Optional<std::string> DeviceTypeToString(cros::mojom::DeviceType type);

cros::mojom::IioChanType ConvertChanType(iio_chan_type chan_type);
cros::mojom::IioEventType ConvertEventType(iio_event_type event_type);
cros::mojom::IioEventDirection ConvertDirection(iio_event_direction direction);

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_COMMON_TYPES_H_
