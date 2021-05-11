// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/event_base.h"

#include "metrics/structured/recorder.h"

namespace metrics {
namespace structured {

EventBase::EventBase(uint64_t event_name_hash, uint64_t project_name_hash)
    : event_name_hash_(event_name_hash),
      project_name_hash_(project_name_hash) {}
EventBase::EventBase(const EventBase& other) = default;
EventBase::~EventBase() = default;

bool EventBase::Record() {
  return Recorder::GetInstance()->Record(*this);
}

void EventBase::AddStringMetric(uint64_t name_hash, const std::string& value) {
  Metric metric(name_hash, MetricType::kString);
  metric.string_value = value;
  metrics_.push_back(metric);
}

void EventBase::AddIntMetric(uint64_t name_hash, int64_t value) {
  Metric metric(name_hash, MetricType::kInt);
  metric.int_value = value;
  metrics_.push_back(metric);
}

EventBase::Metric::Metric(uint64_t name_hash, MetricType type)
    : name_hash(name_hash), type(type) {}
EventBase::Metric::~Metric() = default;

}  // namespace structured
}  // namespace metrics
