// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCP_CONFIG_H_
#define SHILL_NETWORK_DHCP_CONFIG_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/ipconfig.h"
#include "shill/metrics.h"
#include "shill/store/key_value_store.h"
#include "shill/technology.h"

namespace shill {

class ControlInterface;
class DHCPProvider;
class DHCPProxyInterface;
class EventDispatcher;
class Metrics;
class ProcessManager;

// This class provides a DHCP client instance for the device |device_name|.
//
// The DHPCConfig instance asks the DHCP client to create a lease file
// containing the name |lease_file_suffix|.  If this suffix is the same as
// |device_name|, the lease is considered to be ephemeral, and the lease
// file is removed whenever this DHCPConfig instance is no longer needed.
// Otherwise, the lease file persists and will be re-used in future attempts.
class DHCPConfig : public IPConfig {
 public:
  // Called when the IPConfig got from DHCP is updated. The second parameter of
  // UpdateCallback indicates whether or not a DHCP lease was acquired from the
  // server.
  using UpdateCallback =
      base::RepeatingCallback<void(const IPConfigRefPtr&, bool)>;
  // Called when DHCP failed.
  using FailureCallback = base::RepeatingCallback<void(const IPConfigRefPtr&)>;

  DHCPConfig(ControlInterface* control_interface,
             EventDispatcher* dispatcher,
             DHCPProvider* provider,
             const std::string& device_name,
             const std::string& type,
             const std::string& lease_file_suffix,
             Technology technology,
             Metrics* metrics);
  DHCPConfig(const DHCPConfig&) = delete;
  DHCPConfig& operator=(const DHCPConfig&) = delete;

  ~DHCPConfig() override;

  // Registers callbacks for DHCP events.
  void RegisterCallbacks(UpdateCallback update_callback,
                         FailureCallback failure_callback);

  // Inherited from IPConfig.
  bool RequestIP() override;
  bool RenewIP() override;
  bool ReleaseIP(ReleaseReason reason) override;

  // If |proxy_| is not initialized already, sets it to a new D-Bus proxy to
  // |service|.
  void InitProxy(const std::string& service);

  // Processes an Event signal from dhcpcd.
  virtual void ProcessEventSignal(const std::string& reason,
                                  const KeyValueStore& configuration) = 0;

  // Returns the time left (in seconds) till the current DHCP lease is to be
  // renewed in |time_left|. Returns nullopt if an error occurs (i.e. current
  // lease has already expired or no current DHCP lease), true otherwise.
  std::optional<base::TimeDelta> TimeToLeaseExpiry() override;

  // Set the minimum MTU that this configuration will respect.
  virtual void set_minimum_mtu(const int minimum_mtu) {
    minimum_mtu_ = minimum_mtu;
  }

 protected:
  // On we get a new IP config properties via DHCP. The second parameter
  // indicates whether this is an authoritative confirmation.
  void OnIPConfigUpdated(const Properties& properties, bool new_lease_acquired);

  // Notifies registered listeners that the configuration process has failed.
  void NotifyFailure();

  // Notifies registered listeners that the lease has expired.
  void NotifyUpdate(bool new_lease_acquired);

  int minimum_mtu() const { return minimum_mtu_; }

  void set_is_lease_active(bool active) { is_lease_active_ = active; }

  // Return true if the lease file is ephermeral, which means the lease file
  // should be deleted during cleanup.
  bool IsEphemeralLease() const;

  // Cleans up remaining state from a running client, if any, including freeing
  // its GPid, exit watch callback, and state files.
  virtual void CleanupClientState();

  // Return true if we should treat acquisition timeout as failure.
  virtual bool ShouldFailOnAcquisitionTimeout() { return true; }

  // Return true if we should keep the lease on disconnect.
  virtual bool ShouldKeepLeaseOnDisconnect() { return false; }

  // Updates |current_lease_expiration_time_| by adding |new_lease_duration| to
  // the current time.
  void UpdateLeaseExpirationTime(uint32_t new_lease_duration);

  // Resets |current_lease_expiration_time_| to its default value.
  void ResetLeaseExpirationTime();

  // Return the list of flags used to start dhcpcd.
  virtual std::vector<std::string> GetFlags();

  base::FilePath root() const { return root_; }

 private:
  friend class DHCPConfigTest;
  friend class DHCPv4ConfigTest;
  FRIEND_TEST(DHCPConfigCallbackTest, NotifyFailure);
  FRIEND_TEST(DHCPConfigCallbackTest, ProcessAcquisitionTimeout);
  FRIEND_TEST(DHCPConfigCallbackTest, RequestIPTimeout);
  FRIEND_TEST(DHCPConfigCallbackTest, StartTimeout);
  FRIEND_TEST(DHCPConfigCallbackTest, StoppedDuringFailureCallback);
  FRIEND_TEST(DHCPConfigCallbackTest, StoppedDuringSuccessCallback);
  FRIEND_TEST(DHCPConfigTest, InitProxy);
  FRIEND_TEST(DHCPConfigTest, KeepLeaseOnDisconnect);
  FRIEND_TEST(DHCPConfigTest, ReleaseIP);
  FRIEND_TEST(DHCPConfigTest, ReleaseIPStaticIPWithLease);
  FRIEND_TEST(DHCPConfigTest, ReleaseIPStaticIPWithoutLease);
  FRIEND_TEST(DHCPConfigTest, ReleaseLeaseOnDisconnect);
  FRIEND_TEST(DHCPConfigTest, RenewIP);
  FRIEND_TEST(DHCPConfigTest, RequestIP);
  FRIEND_TEST(DHCPConfigTest, Restart);
  FRIEND_TEST(DHCPConfigTest, RestartNoClient);
  FRIEND_TEST(DHCPConfigTest, StartFail);
  FRIEND_TEST(DHCPConfigTest, StartWithoutLeaseSuffix);
  FRIEND_TEST(DHCPConfigTest, Stop);
  FRIEND_TEST(DHCPConfigTest, StopDuringRequestIP);
  FRIEND_TEST(DHCPProviderTest, CreateIPv4Config);

  // Starts dhcpcd, returns true on success and false otherwise.
  bool Start();

  // Stops dhcpcd if running.
  void Stop(const char* reason);

  // Stops dhcpcd if already running and then starts it. Returns true on success
  // and false otherwise.
  bool Restart();

  // Called when the dhcpcd client process exits.
  void OnProcessExited(int exit_status);

  // Initialize a callback that will invoke ProcessAcquisitionTimeout if we
  // do not get a lease in a reasonable amount of time.
  void StartAcquisitionTimeout();
  // Cancel callback created by StartAcquisitionTimeout. One-liner included
  // for symmetry.
  void StopAcquisitionTimeout();
  // Called if we do not get a DHCP lease in a reasonable amount of time.
  // Informs upper layers of the failure.
  void ProcessAcquisitionTimeout();

  // Initialize a callback that will invoke ProcessExpirationTimeout if we
  // do not renew a lease in a |lease_duration|.
  void StartExpirationTimeout(base::TimeDelta lease_duration);
  // Cancel callback created by StartExpirationTimeout. One-liner included
  // for symmetry.
  void StopExpirationTimeout();
  // Called if we do not renew a DHCP lease by the time the lease expires.
  // Informs upper layers of the expiration and restarts the DHCP client.
  void ProcessExpirationTimeout(base::TimeDelta lease_duration);

  // Kills DHCP client process.
  void KillClient();

  ControlInterface* control_interface_;

  DHCPProvider* provider_;

  // DHCP lease file suffix, used to differentiate the lease of one interface
  // or network from another.
  std::string lease_file_suffix_;

  // The technology of device which DHCP is running on.
  Technology technology_;

  // The PID of the spawned DHCP client. May be 0 if no client has been spawned
  // yet or the client has died.
  int pid_;

  // Whether a lease has been acquired from the DHCP server or gateway ARP.
  bool is_lease_active_;

  // The proxy for communicating with the DHCP client.
  std::unique_ptr<DHCPProxyInterface> proxy_;

  // Called if we fail to get a DHCP lease in a timely manner.
  base::CancelableOnceClosure lease_acquisition_timeout_callback_;

  // Time to wait for a DHCP lease. Represented as field so that it
  // can be overridden in tests.
  base::TimeDelta lease_acquisition_timeout_;

  std::optional<struct timeval> current_lease_expiration_time_;

  // Called if a DHCP lease expires.
  base::CancelableOnceClosure lease_expiration_callback_;

  // Callbacks registered by RegisterCallbacks().
  UpdateCallback update_callback_;
  FailureCallback failure_callback_;

  // The minimum MTU value this configuration will respect.
  int minimum_mtu_;

  // Root file path, used for testing.
  base::FilePath root_;

  base::WeakPtrFactory<DHCPConfig> weak_ptr_factory_;
  EventDispatcher* dispatcher_;
  ProcessManager* process_manager_;
  Metrics* metrics_;
  Time* time_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_DHCP_CONFIG_H_