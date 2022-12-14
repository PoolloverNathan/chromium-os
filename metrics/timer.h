// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Timer - class that provides timer tracking.

#ifndef METRICS_TIMER_H_
#define METRICS_TIMER_H_

#include <memory>
#include <string>

#include <base/time/time.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

class MetricsLibraryInterface;

namespace chromeos_metrics {

class TimerInterface {
 public:
  virtual ~TimerInterface() {}

  virtual bool Start() = 0;
  virtual bool Stop() = 0;
  virtual bool Reset() = 0;
  virtual bool HasStarted() const = 0;
};

// Wrapper for calls to the system clock.
class ClockWrapper {
 public:
  ClockWrapper() {}
  ClockWrapper(const ClockWrapper&) = delete;
  ClockWrapper& operator=(const ClockWrapper&) = delete;

  virtual ~ClockWrapper() {}

  // Returns the current time from the system.
  virtual base::TimeTicks GetCurrentTime() const;
};

// Implements a Timer.
class Timer : public TimerInterface {
 public:
  Timer();
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

  virtual ~Timer() {}

  // Starts the timer. If a timer is already running, also resets current
  // timer. Always returns true.
  virtual bool Start();

  // Stops the timer and calculates the total time elapsed between now and when
  // Start() was called. Note that this method needs a prior call to Start().
  // Otherwise, it fails (returns false).
  virtual bool Stop();

  // Pauses a timer.  If the timer is stopped, this call starts the timer in
  // the paused state. Fails (returns false) if the timer is already paused.
  virtual bool Pause();

  // Restarts a paused timer (or starts a stopped timer). This method fails
  // (returns false) if the timer is already running; otherwise, returns true.
  virtual bool Resume();

  // Resets the timer, erasing the current duration being tracked. Always
  // returns true.
  virtual bool Reset();

  // Returns whether the timer has started or not.
  virtual bool HasStarted() const;

  // Stores the current elapsed time in |elapsed_time|. If timer is stopped,
  // stores the elapsed time from when Stop() was last called. Otherwise,
  // calculates and stores the elapsed time since the last Start().
  // Returns false if the timer was never Start()'ed or if called with a null
  // pointer argument.
  virtual bool GetElapsedTime(base::TimeDelta* elapsed_time) const;

 private:
  enum TimerState { kTimerStopped, kTimerRunning, kTimerPaused };
  friend class TimerTest;
  friend class TimerReporterTest;
  FRIEND_TEST(TimerReporterTest, StartStopReport);
  FRIEND_TEST(TimerTest, InvalidElapsedTime);
  FRIEND_TEST(TimerTest, InvalidStop);
  FRIEND_TEST(TimerTest, PauseResumeStop);
  FRIEND_TEST(TimerTest, PauseStartStopResume);
  FRIEND_TEST(TimerTest, PauseStop);
  FRIEND_TEST(TimerTest, Reset);
  FRIEND_TEST(TimerTest, ReStart);
  FRIEND_TEST(TimerTest, ResumeStartStopPause);
  FRIEND_TEST(TimerTest, SeparatedTimers);
  FRIEND_TEST(TimerTest, StartPauseResumePauseResumeStop);
  FRIEND_TEST(TimerTest, StartPauseResumePauseStop);
  FRIEND_TEST(TimerTest, StartPauseResumeStop);
  FRIEND_TEST(TimerTest, StartPauseStop);
  FRIEND_TEST(TimerTest, StartResumeStop);
  FRIEND_TEST(TimerTest, StartStop);

  // Elapsed time of the last use of the timer.
  base::TimeDelta elapsed_time_;

  // Starting time value.
  base::TimeTicks start_time_;

  // Whether the timer is running, stopped, or paused.
  TimerState timer_state_;

  // Wrapper for the calls to the system clock.
  std::unique_ptr<ClockWrapper> clock_wrapper_;
};

// Extends the Timer class to report the elapsed time in milliseconds or
// seconds through the UMA metrics library.
class TimerReporter : public Timer {
 public:
  // Initializes the timer by providing a |histogram_name| to report to with
  // |min|, |max| and |num_buckets| attributes for the histogram.
  TimerReporter(const std::string& histogram_name,
                int min,
                int max,
                int num_buckets);
  TimerReporter(const TimerReporter&) = delete;
  TimerReporter& operator=(const TimerReporter&) = delete;

  virtual ~TimerReporter() {}

  // Sets the metrics library used by all instances of this class.
  static void set_metrics_lib(MetricsLibraryInterface* metrics_lib) {
    metrics_lib_ = metrics_lib;
  }

  // TimerReporter exposes 2 different methods ReportMilliseconds() and
  // ReportSeconds() to allow reporting elapsed time in 2 different units
  // milliseconds and seconds respectively. It is expected that for a given
  // timer reporter instance only one unit of reporting must be used
  // for consistent reporting of elapsed time.

  // Reports the current duration to UMA, in milliseconds. Returns false if
  // there is nothing to report, e.g. a metrics library is not set.
  virtual bool ReportMilliseconds() const;
  // Reports the current duration to UMA, in seconds. Returns false if
  // there is nothing to report, e.g. a metrics library is not set.
  virtual bool ReportSeconds() const;

  // Accessor methods.
  const std::string& histogram_name() const { return histogram_name_; }
  int min() const { return min_; }
  int max() const { return max_; }
  int num_buckets() const { return num_buckets_; }

 private:
  enum TimerUnit { kTimerUnitNone, kTimerUnitMilliseconds, kTimerUnitSeconds };
  friend class TimerReporterTest;
  FRIEND_TEST(TimerReporterTest, StartStopReport);
  FRIEND_TEST(TimerReporterTest, InvalidReport);

  static MetricsLibraryInterface* metrics_lib_;
  std::string histogram_name_;
  int min_;
  int max_;
  int num_buckets_;
  // Unit used by timer reporter for reporting elapsed time.
  mutable TimerUnit timer_unit_;
};

}  // namespace chromeos_metrics

#endif  // METRICS_TIMER_H_
