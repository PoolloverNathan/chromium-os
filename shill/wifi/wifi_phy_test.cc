// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_phy.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/device.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/net/netlink_packet.h"
#include "shill/net/nl80211_attribute.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"

using testing::_;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Test;

namespace shill {

// NL80211_CMD_NEW_WIPHY message which indicates support for the following
// interface types: IBSS, managed, AP, monitor, P2P-client, P2P-GO, P2P-device.
const uint8_t kNewWiphyNlMsg_IfTypes[] = {
    0x50, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf6, 0x31, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x09, 0x00, 0x02, 0x00, 0x70, 0x68, 0x79, 0x37,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x2e, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x20, 0x00, 0x04, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x06, 0x00, 0x04, 0x00, 0x08, 0x00,
    0x04, 0x00, 0x09, 0x00, 0x04, 0x00, 0x0a, 0x00};

const uint8_t kActiveScanTriggerNlMsg[] = {
    0x44, 0x01, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x21, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x99, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x2d, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x01, 0x2c, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x6c, 0x09, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x71, 0x09, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00, 0x76, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x03, 0x00, 0x7b, 0x09, 0x00, 0x00, 0x08, 0x00, 0x04, 0x00,
    0x80, 0x09, 0x00, 0x00, 0x08, 0x00, 0x05, 0x00, 0x85, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x06, 0x00, 0x8a, 0x09, 0x00, 0x00, 0x08, 0x00, 0x07, 0x00,
    0x8f, 0x09, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x94, 0x09, 0x00, 0x00,
    0x08, 0x00, 0x09, 0x00, 0x99, 0x09, 0x00, 0x00, 0x08, 0x00, 0x0a, 0x00,
    0x9e, 0x09, 0x00, 0x00, 0x08, 0x00, 0x0b, 0x00, 0x3c, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x0c, 0x00, 0x50, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0d, 0x00,
    0x64, 0x14, 0x00, 0x00, 0x08, 0x00, 0x0e, 0x00, 0x78, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x0f, 0x00, 0x8c, 0x14, 0x00, 0x00, 0x08, 0x00, 0x10, 0x00,
    0xa0, 0x14, 0x00, 0x00, 0x08, 0x00, 0x11, 0x00, 0xb4, 0x14, 0x00, 0x00,
    0x08, 0x00, 0x12, 0x00, 0xc8, 0x14, 0x00, 0x00, 0x08, 0x00, 0x13, 0x00,
    0x7c, 0x15, 0x00, 0x00, 0x08, 0x00, 0x14, 0x00, 0x90, 0x15, 0x00, 0x00,
    0x08, 0x00, 0x15, 0x00, 0xa4, 0x15, 0x00, 0x00, 0x08, 0x00, 0x16, 0x00,
    0xb8, 0x15, 0x00, 0x00, 0x08, 0x00, 0x17, 0x00, 0xcc, 0x15, 0x00, 0x00,
    0x08, 0x00, 0x18, 0x00, 0x1c, 0x16, 0x00, 0x00, 0x08, 0x00, 0x19, 0x00,
    0x30, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1a, 0x00, 0x44, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x1b, 0x00, 0x58, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1c, 0x00,
    0x71, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x85, 0x16, 0x00, 0x00,
    0x08, 0x00, 0x1e, 0x00, 0x99, 0x16, 0x00, 0x00, 0x08, 0x00, 0x1f, 0x00,
    0xad, 0x16, 0x00, 0x00, 0x08, 0x00, 0x20, 0x00, 0xc1, 0x16, 0x00, 0x00};

uint32_t kWiFiPhyIndex = 0;
class WiFiPhyTest : public ::testing::Test {
 public:
  WiFiPhyTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        wifi_phy_(kWiFiPhyIndex) {}
  ~WiFiPhyTest() override = default;

 protected:
  EventDispatcherForTest dispatcher_;
  MockControl control_interface_;
  NiceMock<MockMetrics> metrics_;
  MockManager manager_;
  WiFiPhy wifi_phy_;

  MockManager* manager() { return &manager_; }

  void AddWiFiDevice(WiFiConstRefPtr device) {
    wifi_phy_.AddWiFiDevice(device);
  }

  void DeleteWiFiDevice(WiFiConstRefPtr device) {
    wifi_phy_.DeleteWiFiDevice(device);
  }

  bool HasWiFiDevice(WiFiConstRefPtr device) {
    return base::Contains(wifi_phy_.wifi_devices_, device);
  }

  void OnNewWiphy(const Nl80211Message& nl80211_message) {
    wifi_phy_.OnNewWiphy(nl80211_message);
  }

  void AddSupportedIface(nl80211_iftype iftype) {
    wifi_phy_.supported_ifaces_.insert(iftype);
  }

  bool SupportsIftype(nl80211_iftype iftype) {
    return wifi_phy_.SupportsIftype(iftype);
  }

  void ParseInterfaceTypes(const Nl80211Message& nl80211_message) {
    wifi_phy_.ParseInterfaceTypes(nl80211_message);
  }
};

TEST_F(WiFiPhyTest, AddAndDeleteDevices) {
  scoped_refptr<MockWiFi> device0 =
      new NiceMock<MockWiFi>(&manager_, "null0", "aabbccddeeff", 0,
                             kWiFiPhyIndex, new MockWakeOnWiFi());
  scoped_refptr<MockWiFi> device1 =
      new NiceMock<MockWiFi>(&manager_, "null1", "ffeeddccbbaa", 0,
                             kWiFiPhyIndex, new MockWakeOnWiFi());

  EXPECT_FALSE(HasWiFiDevice(device0));
  EXPECT_FALSE(HasWiFiDevice(device1));

  AddWiFiDevice(device0);
  EXPECT_TRUE(HasWiFiDevice(device0));
  EXPECT_FALSE(HasWiFiDevice(device1));

  AddWiFiDevice(device1);
  EXPECT_TRUE(HasWiFiDevice(device0));
  EXPECT_TRUE(HasWiFiDevice(device1));

  DeleteWiFiDevice(device0);
  EXPECT_FALSE(HasWiFiDevice(device0));
  EXPECT_TRUE(HasWiFiDevice(device1));

  DeleteWiFiDevice(device1);
  EXPECT_FALSE(HasWiFiDevice(device0));
  EXPECT_FALSE(HasWiFiDevice(device1));
}

TEST_F(WiFiPhyTest, AddDeviceTwice) {
  scoped_refptr<MockWiFi> device =
      new NiceMock<MockWiFi>(&manager_, "null0", "aabbccddeeff", 0,
                             kWiFiPhyIndex, new MockWakeOnWiFi());

  AddWiFiDevice(device);
  EXPECT_TRUE(HasWiFiDevice(device));

  // Adding the same device a second time should be a no-op.
  AddWiFiDevice(device);
  EXPECT_TRUE(HasWiFiDevice(device));

  // The device should be gone after one delete.
  DeleteWiFiDevice(device);
  EXPECT_FALSE(HasWiFiDevice(device));
}

TEST_F(WiFiPhyTest, DeleteDeviceTwice) {
  scoped_refptr<MockWiFi> device =
      new NiceMock<MockWiFi>(&manager_, "null0", "aabbccddeeff", 0,
                             kWiFiPhyIndex, new MockWakeOnWiFi());

  AddWiFiDevice(device);
  EXPECT_TRUE(HasWiFiDevice(device));

  DeleteWiFiDevice(device);
  EXPECT_FALSE(HasWiFiDevice(device));

  // Deleting a device a second time should be a no-op.
  DeleteWiFiDevice(device);
  EXPECT_FALSE(HasWiFiDevice(device));
}

TEST_F(WiFiPhyTest, OnNewWiphy_WrongMessage) {
  ScopedMockLog log;
  TriggerScanMessage msg;
  NetlinkPacket packet(kActiveScanTriggerNlMsg,
                       sizeof(kActiveScanTriggerNlMsg));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       HasSubstr("Received unexpected command")));
  OnNewWiphy(msg);
}

TEST_F(WiFiPhyTest, OnNewWiphy_NoIndex) {
  ScopedMockLog log;
  NewWiphyMessage msg;
  // Do not initialize the message so that it has no NL80211_ATTR_WIPHY.
  EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                       "NL80211_CMD_NEW_WIPHY had no NL80211_ATTR_WIPHY"));
  OnNewWiphy(msg);
}

TEST_F(WiFiPhyTest, OnNewWiphy_WrongIndex) {
  ScopedMockLog log;
  NewWiphyMessage msg;
  NetlinkPacket packet(kNewWiphyNlMsg_IfTypes, sizeof(kNewWiphyNlMsg_IfTypes));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  EXPECT_CALL(
      log, Log(logging::LOGGING_ERROR, _,
               HasSubstr(
                   "received NL80211_CMD_NEW_WIPHY for unexpected phy index")));
  OnNewWiphy(msg);
}

TEST_F(WiFiPhyTest, SupportsIftype) {
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_AP));
  AddSupportedIface(NL80211_IFTYPE_AP);
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_AP));
}

TEST_F(WiFiPhyTest, ParseInterfaceTypes) {
  NewWiphyMessage msg;
  NetlinkPacket packet(kNewWiphyNlMsg_IfTypes, sizeof(kNewWiphyNlMsg_IfTypes));
  msg.InitFromPacket(&packet, NetlinkMessage::MessageContext());
  ParseInterfaceTypes(msg);
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_ADHOC));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_STATION));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_AP));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_MONITOR));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_P2P_CLIENT));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_P2P_GO));
  EXPECT_TRUE(SupportsIftype(NL80211_IFTYPE_P2P_DEVICE));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_AP_VLAN));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_WDS));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_MESH_POINT));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_OCB));
  EXPECT_FALSE(SupportsIftype(NL80211_IFTYPE_NAN));
}

}  // namespace shill
