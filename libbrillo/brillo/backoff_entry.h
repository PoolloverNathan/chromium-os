// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_BACKOFF_ENTRY_H_
#define LIBBRILLO_BRILLO_BACKOFF_ENTRY_H_

#include <base/time/time.h>
#include <brillo/brillo_export.h>

namespace brillo {

// Provides the core logic needed for randomized exponential back-off
// on requests to a given resource, given a back-off policy.
//
// This class is largely taken from net/base/backoff_entry.h from Chromium.
// TODO(avakulenko): Consider packaging portions of Chrome's //net functionality
// into the current libchrome library.
class BRILLO_EXPORT BackoffEntry {
 public:
  // The set of parameters that define a back-off policy.
  struct Policy {
    // Number of initial errors (in sequence) to ignore before applying
    // exponential back-off rules.
    int num_errors_to_ignore;

    // Initial delay.  The interpretation of this value depends on
    // always_use_initial_delay.  It's either how long we wait between
    // requests before backoff starts, or how much we delay the first request
    // after backoff starts.
    int initial_delay_ms;

    // Factor by which the waiting time will be multiplied.
    double multiply_factor;

    // Fuzzing percentage. ex: 10% will spread requests randomly
    // between 90%-100% of the calculated time.
    double jitter_factor;

    // Maximum amount of time we are willing to delay our request, -1
    // for no maximum.
    int64_t maximum_backoff_ms;

    // Time to keep an entry from being discarded even when it
    // has no significant state, -1 to never discard.
    int64_t entry_lifetime_ms;

    // If true, we always use a delay of initial_delay_ms, even before
    // we've seen num_errors_to_ignore errors.  Otherwise, initial_delay_ms
    // is the first delay once we start exponential backoff.
    //
    // So if we're ignoring 1 error, we'll see (N, N, Nm, Nm^2, ...) if true,
    // and (0, 0, N, Nm, ...) when false, where N is initial_backoff_ms and
    // m is multiply_factor, assuming we've already seen one success.
    bool always_use_initial_delay;
  };

  // Lifetime of policy must enclose lifetime of BackoffEntry. The
  // pointer must be valid but is not dereferenced during construction.
  explicit BackoffEntry(const Policy* const policy);
  BackoffEntry(const BackoffEntry&) = delete;
  BackoffEntry& operator=(const BackoffEntry&) = delete;

  virtual ~BackoffEntry() = default;

  // Inform this item that a request for the network resource it is
  // tracking was made, and whether it failed or succeeded.
  void InformOfRequest(bool succeeded);

  // Returns true if a request for the resource this item tracks should
  // be rejected at the present time due to exponential back-off policy.
  bool ShouldRejectRequest() const;

  // Returns the absolute time after which this entry (given its present
  // state) will no longer reject requests.
  base::TimeTicks GetReleaseTime() const;

  // Returns the time until a request can be sent.
  base::TimeDelta GetTimeUntilRelease() const;

  // Causes this object reject requests until the specified absolute time.
  // This can be used to e.g. implement support for a Retry-After header.
  void SetCustomReleaseTime(const base::TimeTicks& release_time);

  // Returns true if this object has no significant state (i.e. you could
  // just as well start with a fresh BackoffEntry object), and hasn't
  // had for Policy::entry_lifetime_ms.
  bool CanDiscard() const;

  // Resets this entry to a fresh (as if just constructed) state.
  void Reset();

  // Returns the failure count for this entry.
  int failure_count() const { return failure_count_; }

 protected:
  // Equivalent to TimeTicks::Now(), virtual so unit tests can override.
  virtual base::TimeTicks ImplGetTimeNow() const;

 private:
  // Calculates when requests should again be allowed through.
  base::TimeTicks CalculateReleaseTime() const;

  // Timestamp calculated by the exponential back-off algorithm at which we are
  // allowed to start sending requests again.
  base::TimeTicks exponential_backoff_release_time_;

  // Counts request errors; decremented on success.
  int failure_count_;

  const Policy* const policy_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_BACKOFF_ENTRY_H_
