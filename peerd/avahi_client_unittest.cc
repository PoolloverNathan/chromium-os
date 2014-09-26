// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <avahi-common/defs.h>
#include <base/memory/ref_counted.h>
#include <chromeos/dbus/data_serialization.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "peerd/avahi_client.h"
#include "peerd/complete_mock_object_proxy.h"
#include "peerd/dbus_constants.h"
#include "peerd/test_util.h"

using chromeos::dbus_utils::AsyncEventSequencer;
using dbus::MockBus;
using dbus::ObjectPath;
using dbus::Response;
using peerd::dbus_constants::avahi::kServerInterface;
using peerd::dbus_constants::avahi::kServerMethodGetHostName;
using peerd::dbus_constants::avahi::kServerMethodGetState;
using peerd::dbus_constants::avahi::kServerPath;
using peerd::dbus_constants::avahi::kServerSignalStateChanged;
using peerd::dbus_constants::avahi::kServiceName;
using peerd::test_util::IsDBusMethodCallTo;
using testing::AnyNumber;
using testing::Invoke;
using testing::Property;
using testing::Return;
using testing::Unused;
using testing::_;

namespace {

Response* ReturnsHostName(dbus::MethodCall* method_call, Unused, Unused) {
  method_call->SetSerial(87);
  scoped_ptr<Response> response = Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  chromeos::dbus_utils::AppendValueToWriter(&writer,
                                            std::string("this_is_a_hostname"));
  return response.release();
}

}  // namespace


namespace peerd {

class AvahiClientTest : public ::testing::Test {
 public:
  void SetUp() override {
    avahi_proxy_ = new peerd::CompleteMockObjectProxy(
        mock_bus_.get(), kServiceName, ObjectPath(kServerPath));
    // Ignore threading concerns.
    EXPECT_CALL(*mock_bus_, AssertOnOriginThread()).Times(AnyNumber());
    EXPECT_CALL(*mock_bus_, AssertOnDBusThread()).Times(AnyNumber());
    // Return the ObjectProxy for Avahi when asked.
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(kServiceName,
                               Property(&ObjectPath::value, kServerPath)))
        .WillRepeatedly(Return(avahi_proxy_.get()));
    client_.reset(new AvahiClient(mock_bus_));
  }

  void TearDown() override {
    EXPECT_CALL(*avahi_proxy_, Detach()).Times(1);
  }

  void RegisterAsyncWhenAvahiIs(bool is_running, AvahiServerState state) {
    // Immediately signal that signal connection has succeeded.
    EXPECT_CALL(*avahi_proxy_,
                ConnectToSignal(kServerInterface,
                                kServerSignalStateChanged,
                                _, _))
        .WillOnce(Invoke(&test_util::HandleConnectToSignal));
    // After we connect the signal, we'll be asked to handle a callback
    // for the initial state of Avahi.
    EXPECT_CALL(*avahi_proxy_, WaitForServiceToBeAvailable(_));
    if (is_running) {
      EXPECT_CALL(*avahi_proxy_,
                  MockCallMethodAndBlockWithErrorDetails(IsDBusMethodCallTo(
                      kServerInterface, kServerMethodGetState), _, _))
          .WillOnce(Invoke(this, &AvahiClientTest::ReturnsCurrentState));
      EXPECT_CALL(*avahi_proxy_,
                  MockCallMethodAndBlockWithErrorDetails(IsDBusMethodCallTo(
                      kServerInterface, kServerMethodGetHostName), _, _))
          .WillOnce(Invoke(&ReturnsHostName));
    } else {
      EXPECT_CALL(*avahi_proxy_,
                  MockCallMethodAndBlockWithErrorDetails(IsDBusMethodCallTo(
                      kServerInterface, kServerMethodGetState), _, _))
          .Times(0);
      EXPECT_CALL(*avahi_proxy_,
                  MockCallMethodAndBlockWithErrorDetails(IsDBusMethodCallTo(
                      kServerInterface, kServerMethodGetHostName), _, _))
          .Times(0);
    }
    current_avahi_state_ = state;
    client_->RegisterAsync(AsyncEventSequencer::GetDefaultCompletionAction());
    // This is the callback that would normally require a task runner.
    client_->OnServiceAvailable(is_running);
  }

  Response *ReturnsCurrentState(dbus::MethodCall* method_call, Unused, Unused) {
    method_call->SetSerial(87);
    scoped_ptr<Response> response = Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());
    chromeos::dbus_utils::AppendValueToWriter(&writer,
                                              int32_t{current_avahi_state_});
    // The mock wraps this back in a scoped_ptr in the function calling us.
    return response.release();
  }

  void SendOnStateChanged(AvahiServerState state) {
    if (state == AVAHI_SERVER_RUNNING) {
      // Any time we transition to UP we're going to want the hostname again.
      EXPECT_CALL(*avahi_proxy_,
                  MockCallMethodAndBlockWithErrorDetails(IsDBusMethodCallTo(
                      kServerInterface, kServerMethodGetHostName), _, _))
          .WillOnce(Invoke(&ReturnsHostName));
    }
    dbus::Signal signal(kServerInterface, kServerSignalStateChanged);
    dbus::MessageWriter writer(&signal);
    chromeos::dbus_utils::AppendValueToWriter(&writer, int32_t{state});
    // No error message to report cap'n.
    chromeos::dbus_utils::AppendValueToWriter(&writer, "");
    client_->OnServerStateChanged(&signal);
  }

  MOCK_METHOD0(AvahiReady, void());

  scoped_refptr<MockBus> mock_bus_{new MockBus{dbus::Bus::Options{}}};
  scoped_refptr<peerd::CompleteMockObjectProxy> avahi_proxy_;
  std::unique_ptr<AvahiClient> client_;
  AvahiServerState current_avahi_state_;
};  // class AvahiClientTest


TEST_F(AvahiClientTest, ShouldNotifyRestart_WhenAvahiIsUp) {
  // We don't get called back until we finish RegisterAsync().
  EXPECT_CALL(*this, AvahiReady()).Times(0);
  client_->RegisterOnAvahiRestartCallback(
      base::Bind(&AvahiClientTest::AvahiReady, base::Unretained(this)));
  // However, on finishing RegisterAsync(), we should notice Avahi is up, and
  // signal this to interested parties.
  EXPECT_CALL(*this, AvahiReady()).Times(1);
  RegisterAsyncWhenAvahiIs(true, AVAHI_SERVER_RUNNING);
}

TEST_F(AvahiClientTest, ShouldNotifyRestart_WhenAvahiComesUpLater) {
  // Avahi should remain down until we say otherwise.
  EXPECT_CALL(*this, AvahiReady()).Times(0);
  client_->RegisterOnAvahiRestartCallback(
      base::Bind(&AvahiClientTest::AvahiReady, base::Unretained(this)));
  RegisterAsyncWhenAvahiIs(false, AVAHI_SERVER_INVALID);
  // The StateChanged signal should trigger our callback.
  EXPECT_CALL(*this, AvahiReady()).Times(1);
  SendOnStateChanged(AVAHI_SERVER_RUNNING);
}

}  // namespace peerd
