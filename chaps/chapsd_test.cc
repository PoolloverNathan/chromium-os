// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "chaps/chaps_interface.h"
#include "chaps/chaps_proxy.h"
#include "chaps/chaps_service_redirect.h"
#include "chaps/chaps_utility.h"

using std::string;
using std::vector;

DEFINE_bool(use_dbus, false, "");

namespace chaps {

static chaps::ChapsInterface* CreateChapsInstance() {
  if (FLAGS_use_dbus) {
    scoped_ptr<chaps::ChapsProxyImpl> proxy(new chaps::ChapsProxyImpl());
    if (proxy->Init())
      return proxy.release();
  } else {
    scoped_ptr<chaps::ChapsServiceRedirect> service(
        new chaps::ChapsServiceRedirect("libopencryptoki.so"));
    if (service->Init())
      return service.release();
  }
  return NULL;
}

// Default test fixture for PKCS #11 calls.
class TestP11 : public ::testing::Test {
protected:
  virtual void SetUp() {
    // The current user's token will be used so the token will already be
    // initialized and changes to token objects will persist.  The user pin can
    // be assumed to be "111111" and the so pin can be assumed to be "000000".
    // This approach will be used as long as we redirect to openCryptoki.
    chaps_.reset(CreateChapsInstance());
    ASSERT_TRUE(chaps_ != NULL);
  }
  scoped_ptr<chaps::ChapsInterface> chaps_;
};

// Test fixture for testing with a valid open session.
class TestP11Session : public TestP11 {
 protected:
  virtual void SetUp() {
    TestP11::SetUp();
    EXPECT_EQ(CKR_OK, chaps_->OpenSession(0, CKF_SERIAL_SESSION|CKF_RW_SESSION,
                                          &session_id_));
    EXPECT_EQ(CKR_OK, chaps_->OpenSession(0, CKF_SERIAL_SESSION,
                                          &readonly_session_id_));
  }
  virtual void TearDown() {
    EXPECT_EQ(CKR_OK, chaps_->CloseSession(session_id_));
    EXPECT_EQ(CKR_OK, chaps_->CloseSession(readonly_session_id_));
    TestP11::TearDown();
  }
  uint32_t session_id_;
  uint32_t readonly_session_id_;
};

TEST_F(TestP11, SlotList) {
  vector<uint32_t> slot_list;
  uint32_t result = chaps_->GetSlotList(false, &slot_list);
  EXPECT_EQ(CKR_OK, result);
  EXPECT_LT(0, slot_list.size());
  printf("Slots: ");
  for (size_t i = 0; i < slot_list.size(); ++i) {
    printf("%d ", slot_list[i]);
  }
  printf("\n");
  result = chaps_->GetSlotList(false, NULL);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
}

TEST_F(TestP11, SlotInfo) {
  string description;
  string manufacturer;
  uint32_t flags;
  uint8_t version[4];
  uint32_t result = chaps_->GetSlotInfo(0, &description,
                                        &manufacturer, &flags,
                                        &version[0], &version[1],
                                        &version[2], &version[3]);
  EXPECT_EQ(CKR_OK, result);
  printf("Slot Info: %s - %s\n", manufacturer.c_str(), description.c_str());
  result = chaps_->GetSlotInfo(0, NULL, &manufacturer, &flags,
                               &version[0], &version[1],
                               &version[2], &version[3]);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
  result = chaps_->GetSlotInfo(17, &description, &manufacturer, &flags,
                               &version[0], &version[1],
                               &version[2], &version[3]);
  EXPECT_NE(CKR_OK, result);
}

TEST_F(TestP11, TokenInfo) {
  string label;
  string manufacturer;
  string model;
  string serial_number;
  uint32_t flags;
  uint8_t version[4];
  uint32_t not_used[10];
  uint32_t result = chaps_->GetTokenInfo(0, &label, &manufacturer,
                                         &model, &serial_number, &flags,
                                         &not_used[0], &not_used[1],
                                         &not_used[2], &not_used[3],
                                         &not_used[4], &not_used[5],
                                         &not_used[6], &not_used[7],
                                         &not_used[8], &not_used[9],
                                         &version[0], &version[1],
                                         &version[2], &version[3]);
  EXPECT_EQ(CKR_OK, result);
  printf("Token Info: %s - %s - %s - %s\n",
         manufacturer.c_str(),
         model.c_str(),
         label.c_str(),
         serial_number.c_str());
  result = chaps_->GetTokenInfo(0, NULL, &manufacturer,
                                &model, &serial_number, &flags,
                                &not_used[0], &not_used[1],
                                &not_used[2], &not_used[3],
                                &not_used[4], &not_used[5],
                                &not_used[6], &not_used[7],
                                &not_used[8], &not_used[9],
                                &version[0], &version[1],
                                &version[2], &version[3]);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
  result = chaps_->GetTokenInfo(17, &label, &manufacturer,
                                &model, &serial_number, &flags,
                                &not_used[0], &not_used[1],
                                &not_used[2], &not_used[3],
                                &not_used[4], &not_used[5],
                                &not_used[6], &not_used[7],
                                &not_used[8], &not_used[9],
                                &version[0], &version[1],
                                &version[2], &version[3]);
  EXPECT_NE(CKR_OK, result);
}

TEST_F(TestP11, MechList) {
  vector<uint32_t> mech_list;
  uint32_t result = chaps_->GetMechanismList(0, &mech_list);
  EXPECT_EQ(CKR_OK, result);
  EXPECT_LT(0, mech_list.size());
  printf("Mech List [0]: %d\n", mech_list[0]);
  result = chaps_->GetMechanismList(0, NULL);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
  result = chaps_->GetMechanismList(17, &mech_list);
  EXPECT_NE(CKR_OK, result);
}

TEST_F(TestP11, MechInfo) {
  uint32_t flags;
  uint32_t min_key_size;
  uint32_t max_key_size;
  uint32_t result = chaps_->GetMechanismInfo(0, CKM_RSA_PKCS, &min_key_size,
                                         &max_key_size, &flags);
  EXPECT_EQ(CKR_OK, result);
  printf("RSA Key Sizes: %d - %d\n", min_key_size, max_key_size);
  result = chaps_->GetMechanismInfo(0,
                                    0xFFFF,
                                    &min_key_size, &max_key_size,
                                    &flags);
  EXPECT_EQ(CKR_MECHANISM_INVALID, result);
  result = chaps_->GetMechanismInfo(17,
                                    CKM_RSA_PKCS,
                                    &min_key_size,
                                    &max_key_size,
                                    &flags);
  EXPECT_NE(CKR_OK, result);
  result = chaps_->GetMechanismInfo(0,
                                    CKM_RSA_PKCS,
                                    NULL,
                                    &max_key_size,
                                    &flags);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
  result = chaps_->GetMechanismInfo(0,
                                    CKM_RSA_PKCS,
                                    &min_key_size,
                                    NULL,
                                    &flags);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
  result = chaps_->GetMechanismInfo(0,
                                    CKM_RSA_PKCS,
                                    &min_key_size,
                                    &max_key_size,
                                    NULL);
  EXPECT_EQ(CKR_ARGUMENTS_BAD, result);
}

TEST_F(TestP11, InitToken) {
  string pin = "test";
  string label = "test";
  EXPECT_NE(CKR_OK, chaps_->InitToken(0, &pin, label));
  EXPECT_NE(CKR_OK, chaps_->InitToken(0, NULL, label));
}

TEST_F(TestP11Session, InitPIN) {
  string pin = "test";
  EXPECT_NE(CKR_OK, chaps_->InitPIN(session_id_, &pin));
  EXPECT_NE(CKR_OK, chaps_->InitPIN(session_id_, NULL));
}

TEST_F(TestP11Session, SetPIN) {
  string pin = "test";
  string pin2 = "test2";
  EXPECT_NE(CKR_OK, chaps_->SetPIN(session_id_, &pin, &pin2));
  EXPECT_NE(CKR_OK, chaps_->SetPIN(session_id_, NULL, NULL));
}

TEST_F(TestP11, OpenCloseSession) {
  uint32_t session = 0;
  // Test successful RO and RW sessions.
  EXPECT_EQ(CKR_OK, chaps_->OpenSession(0, CKF_SERIAL_SESSION, &session));
  EXPECT_EQ(CKR_OK, chaps_->CloseSession(session));
  EXPECT_EQ(CKR_OK, chaps_->OpenSession(0, CKF_SERIAL_SESSION|CKF_RW_SESSION,
                                        &session));
  EXPECT_EQ(CKR_OK, chaps_->CloseSession(session));
  // Test double close.
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID, chaps_->CloseSession(session));
  // Test error cases.
  EXPECT_EQ(CKR_ARGUMENTS_BAD,
            chaps_->OpenSession(0, CKF_SERIAL_SESSION, NULL));
  EXPECT_EQ(CKR_SESSION_PARALLEL_NOT_SUPPORTED,
            chaps_->OpenSession(0, 0, &session));
  // Test CloseAllSessions.
  EXPECT_EQ(CKR_OK, chaps_->OpenSession(0, CKF_SERIAL_SESSION, &session));
  EXPECT_EQ(CKR_OK, chaps_->CloseAllSessions(0));
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID, chaps_->CloseSession(session));
}

TEST_F(TestP11Session, GetSessionInfo) {
  uint32_t slot_id, state, flags, device_error;
  EXPECT_EQ(CKR_OK, chaps_->GetSessionInfo(session_id_, &slot_id, &state,
                                           &flags, &device_error));
  EXPECT_EQ(0, slot_id);
  EXPECT_EQ(CKS_RW_PUBLIC_SESSION, state);
  EXPECT_EQ(CKF_SERIAL_SESSION|CKF_RW_SESSION, flags);
  EXPECT_EQ(CKR_OK, chaps_->GetSessionInfo(readonly_session_id_, &slot_id,
                                           &state, &flags, &device_error));
  EXPECT_EQ(0, slot_id);
  EXPECT_EQ(CKS_RO_PUBLIC_SESSION, state);
  EXPECT_EQ(CKF_SERIAL_SESSION, flags);
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID,
            chaps_->GetSessionInfo(17, &slot_id, &state, &flags,
                                   &device_error));
  EXPECT_EQ(CKR_ARGUMENTS_BAD, chaps_->GetSessionInfo(session_id_,
                                                      NULL,
                                                      &state,
                                                      &flags,
                                                      &device_error));
  EXPECT_EQ(CKR_ARGUMENTS_BAD, chaps_->GetSessionInfo(session_id_,
                                                      &slot_id,
                                                      NULL,
                                                      &flags,
                                                      &device_error));
  EXPECT_EQ(CKR_ARGUMENTS_BAD, chaps_->GetSessionInfo(session_id_,
                                                      &slot_id,
                                                      &state,
                                                      NULL,
                                                      &device_error));
  EXPECT_EQ(CKR_ARGUMENTS_BAD, chaps_->GetSessionInfo(session_id_,
                                                      &slot_id,
                                                      &state,
                                                      &flags,
                                                      NULL));
}

TEST_F(TestP11Session, GetOperationState) {
  vector<uint8_t> state;
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID, chaps_->GetOperationState(17, &state));
  EXPECT_EQ(CKR_ARGUMENTS_BAD, chaps_->GetOperationState(session_id_, NULL));
}

TEST_F(TestP11Session, SetOperationState) {
  vector<uint8_t> state(10, 0);
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID,
            chaps_->SetOperationState(17, state, 0, 0));
}

TEST_F(TestP11Session, Login) {
  string pin = "test";
  EXPECT_NE(CKR_OK, chaps_->Login(session_id_, CKU_USER, &pin));
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID, chaps_->Login(17, CKU_USER, &pin));
  EXPECT_EQ(CKR_USER_TYPE_INVALID, chaps_->Login(session_id_, 17, &pin));
  EXPECT_NE(CKR_OK, chaps_->Login(session_id_, CKU_USER, NULL));
}

TEST_F(TestP11Session, Logout) {
  EXPECT_EQ(CKR_USER_NOT_LOGGED_IN, chaps_->Logout(session_id_));
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID, chaps_->Logout(17));
}

TEST_F(TestP11Session, CreateObject) {
  CK_OBJECT_CLASS class_value = CKO_DATA;
  CK_UTF8CHAR label[] = "A data object";
  CK_UTF8CHAR application[] = "An application";
  CK_BYTE data[] = "Sample data";
  CK_BYTE data2[] = "Sample data 2";
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE attributes[] = {
    {CKA_CLASS, &class_value, sizeof(class_value)},
    {CKA_TOKEN, &false_value, sizeof(false_value)},
    {CKA_LABEL, label, sizeof(label)-1},
    {CKA_APPLICATION, application, sizeof(application)-1},
    {CKA_VALUE, data, sizeof(data)}
  };
  CK_ATTRIBUTE attributes2[] = {
    {CKA_VALUE, data2, sizeof(data2)}
  };
  AttributeValueMap attribute_map = EncodeAttributes(attributes, 5);
  uint32_t handle = 0;
  EXPECT_EQ(CKR_ARGUMENTS_BAD,
            chaps_->CreateObject(session_id_, attribute_map, NULL));
  EXPECT_EQ(CKR_OK, chaps_->CreateObject(session_id_, attribute_map, &handle));
  AttributeValueMap attribute_map2 = EncodeAttributes(attributes2, 1);
  uint32_t handle2 = 0;
  EXPECT_EQ(CKR_OK,
            chaps_->CopyObject(session_id_, handle, attribute_map2, &handle2));
  EXPECT_EQ(CKR_OK, chaps_->DestroyObject(session_id_, handle));
  EXPECT_EQ(CKR_OK, chaps_->DestroyObject(session_id_, handle2));
  EXPECT_EQ(CKR_SESSION_HANDLE_INVALID,
            chaps_->CreateObject(17, attribute_map, &handle));
  EXPECT_EQ(CKR_TEMPLATE_INCOMPLETE,
            chaps_->CreateObject(session_id_, attribute_map2, &handle));
}

} // namespace chaps

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
