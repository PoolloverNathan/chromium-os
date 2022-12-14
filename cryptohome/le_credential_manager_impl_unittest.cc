// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functional tests for LECredentialManager + SignInHashTree.
#include <iterator>  // For std::begin()/std::end().
#include <memory>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest_prod.h>
#include <libhwsec/factory/tpm2_simulator_factory_for_test.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/error/utilities.h"
#include "cryptohome/le_credential_manager_impl.h"

using ::hwsec_foundation::GetSecureRandom;

namespace {

constexpr int kLEMaxIncorrectAttempt = 5;
constexpr int kFakeLogSize = 2;
constexpr uint8_t kAuthChannel = 0;

// All the keys are 32 bytes long.
constexpr uint8_t kLeSecret1Array[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x02};

constexpr uint8_t kLeSecret2Array[] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x10, 0x12};

constexpr uint8_t kHeSecret1Array[] = {
    0x00, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x00,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x00, 0x06,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

constexpr uint8_t kResetSecret1Array[] = {
    0x00, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

constexpr uint8_t kClientNonceArray[] = {
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x00, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

constexpr char kCredDirName[] = "low_entropy_creds";

// As the point needs to be valid, the point is pre-generated.
constexpr char kClientEccPointXHex[] =
    "78D184E439FD4EC5BADC5431C8A6DD8EC039F945E7AD9DEDC5166BEF390E9AFD";
constexpr char kClientEccPointYHex[] =
    "4E411B61F1B48601ED3A218E4EE6075A3053130E6F25BBFF7FE08BB6D3EC6BF6";

}  // namespace

namespace cryptohome {

class LECredentialManagerImplUnitTest : public testing::Test {
 public:
  LECredentialManagerImplUnitTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    InitLEManager();
  }

  // Returns location of on-disk hash tree directory.
  base::FilePath CredDirPath() {
    return temp_dir_.GetPath().Append(kCredDirName);
  }

  void InitLEManager() {
    le_mgr_ = std::make_unique<LECredentialManagerImpl>(pinweaver_.get(),
                                                        CredDirPath());
  }

  // Helper function to create a credential & then lock it out.
  // NOTE: Parameterize the secrets once you have more than 1
  // of them.
  uint64_t CreateLockedOutCredential() {
    std::map<uint32_t, uint32_t> delay_sched = {
        {kLEMaxIncorrectAttempt, UINT32_MAX},
    };
    uint64_t label;
    brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                  std::end(kLeSecret1Array));
    brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                  std::end(kHeSecret1Array));
    brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                     std::end(kResetSecret1Array));

    EXPECT_TRUE(le_mgr_
                    ->InsertCredential(
                        std::vector<hwsec::OperationPolicySetting>(),
                        kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                        /*expiration_delay=*/std::nullopt, &label)
                    .ok());

    brillo::SecureBlob he_secret;
    brillo::SecureBlob reset_secret;
    for (int i = 0; i < kLEMaxIncorrectAttempt; i++) {
      EXPECT_EQ(
          LE_CRED_ERROR_INVALID_LE_SECRET,
          le_mgr_->CheckCredential(label, kHeSecret1, &he_secret, &reset_secret)
              ->local_lecred_error());
    }
    return label;
  }

  // Corrupts |path| by replacing file contents with random data.
  void CorruptFile(base::FilePath path) {
    int64_t file_size;
    ASSERT_TRUE(base::GetFileSize(path, &file_size));
    std::vector<uint8_t> random_data(file_size);
    GetSecureRandom(random_data.data(), file_size);
    ASSERT_EQ(file_size,
              base::WriteFile(path, reinterpret_cast<char*>(random_data.data()),
                              file_size));
  }

  void CorruptLeafCache() {
    // Fill the leafcache file with random data.
    base::FilePath leaf_cache = CredDirPath().Append(kLeafCacheFileName);
    CorruptFile(leaf_cache);
  }

  // Corrupts all versions of the |label| leaf. We corrupt all the versions,
  // since it is tedious to find which is the most recent one.
  void CorruptHashTreeWithLabel(uint64_t label) {
    base::FilePath leaf_dir = CredDirPath().Append(std::to_string(label));
    ASSERT_TRUE(base::PathExists(leaf_dir));
    ASSERT_FALSE(leaf_dir.empty());

    base::FileEnumerator files(leaf_dir, false, base::FileEnumerator::FILES);
    for (base::FilePath cur_file = files.Next(); !cur_file.empty();
         cur_file = files.Next()) {
      CorruptFile(cur_file);
    }
  }

  // Takes a snapshot of the on-disk hash three, and returns the directory
  // where the snapshot is stored.
  std::unique_ptr<base::ScopedTempDir> CaptureSnapshot() {
    auto snapshot = std::make_unique<base::ScopedTempDir>();
    CHECK(snapshot->CreateUniqueTempDir());
    base::CopyDirectory(CredDirPath(), snapshot->GetPath(), true);

    return snapshot;
  }

  // Fills the on-disk hash tree with the contents of |snapshot_path|.
  void RestoreSnapshot(base::FilePath snapshot_path) {
    ASSERT_TRUE(base::DeletePathRecursively(CredDirPath()));
    ASSERT_TRUE(base::CopyDirectory(snapshot_path.Append(kCredDirName),
                                    temp_dir_.GetPath(), true));
  }

  void GeneratePk(uint8_t auth_channel) {
    hwsec::PinWeaverFrontend::PinWeaverEccPoint pt;
    brillo::Blob x_blob, y_blob;
    base::HexStringToBytes(kClientEccPointXHex, &x_blob);
    base::HexStringToBytes(kClientEccPointYHex, &y_blob);
    memcpy(pt.x, x_blob.data(), sizeof(pt.x));
    memcpy(pt.y, y_blob.data(), sizeof(pt.y));
    EXPECT_TRUE(pinweaver_->GeneratePk(auth_channel, pt).ok());
  }

  // Helper function to create a rate-limiter & then lock it out.
  uint64_t CreateLockedOutRateLimiter(uint8_t auth_channel) {
    const std::map<uint32_t, uint32_t> delay_sched = {
        {kLEMaxIncorrectAttempt, UINT32_MAX},
    };
    brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                    std::end(kClientNonceArray));
    const brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                           std::end(kResetSecret1Array));

    uint64_t label;
    EXPECT_TRUE(
        le_mgr_
            ->InsertRateLimiter(auth_channel,
                                std::vector<hwsec::OperationPolicySetting>(),
                                kResetSecret1, delay_sched,
                                /*expiration_delay=*/std::nullopt, &label)
            .ok());

    for (int i = 0; i < kLEMaxIncorrectAttempt; i++) {
      EXPECT_TRUE(
          le_mgr_->StartBiometricsAuth(auth_channel, label, kClientNonce).ok());
    }
    return label;
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_dir_;
  hwsec::Tpm2SimulatorFactoryForTest factory_;
  std::unique_ptr<hwsec::PinWeaverFrontend> pinweaver_{
      factory_.GetPinWeaverFrontend()};
  std::unique_ptr<LECredentialManager> le_mgr_;
};

// Basic check: Insert 2 labels, then verify we can retrieve them correctly.
// Here, we don't bother with specifying a delay schedule, we just want
// to check whether a simple Insert and Check works.
TEST_F(LECredentialManagerImplUnitTest, BasicInsertAndCheck) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  uint64_t label1;
  uint64_t label2;
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kLeSecret2(std::begin(kLeSecret2Array),
                                std::end(kLeSecret2Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_EQ(he_secret, kHeSecret1);
  EXPECT_EQ(
      LE_CRED_ERROR_INVALID_LE_SECRET,
      le_mgr_->CheckCredential(label2, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label2, kLeSecret2, &he_secret, &reset_secret)
          .ok());
  EXPECT_EQ(he_secret, kHeSecret1);
}

// Basic check: Insert 2 rate-limiters, then verify we can retrieve them
// correctly.
TEST_F(LECredentialManagerImplUnitTest, BiometricsBasicInsertAndCheck) {
  constexpr uint8_t kWrongAuthChannel = 1;
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  uint64_t label1;
  uint64_t label2;
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));
  brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                  std::end(kClientNonceArray));

  GeneratePk(kAuthChannel);
  EXPECT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  EXPECT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &label2)
          .ok());
  auto reply1 =
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce);
  ASSERT_TRUE(reply1.ok());

  auto reply2 =
      le_mgr_->StartBiometricsAuth(kAuthChannel, label2, kClientNonce);
  ASSERT_TRUE(reply2.ok());

  // Server should return different values every time.
  EXPECT_NE(reply1->server_nonce, reply2->server_nonce);
  EXPECT_NE(reply1->iv, reply2->iv);
  EXPECT_NE(reply1->encrypted_he_secret, reply2->encrypted_he_secret);

  // Incorrect auth channel passed should result in INVALID_LE_SECRET.
  GeneratePk(kWrongAuthChannel);
  EXPECT_EQ(
      LE_CRED_ERROR_INVALID_LE_SECRET,
      le_mgr_->StartBiometricsAuth(kWrongAuthChannel, label1, kClientNonce)
          .status()
          ->local_lecred_error());
}

// Insert a label and verify that authentication works. Simulate the PCR
// change with the right value and check that authentication still works.
// Change PCR with wrong value and check that authentication fails.
TEST_F(LECredentialManagerImplUnitTest, CheckPcrAuth) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  std::vector<hwsec::OperationPolicySetting> policies = {
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = "obfuscated_username",
                      },
              },
      },
  };
  uint64_t label1;
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  ASSERT_TRUE(le_mgr_
                  ->InsertCredential(policies, kLeSecret1, kHeSecret1,
                                     kResetSecret1, delay_sched,
                                     /*expiration_delay=*/std::nullopt, &label1)
                  .ok());
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());

  EXPECT_EQ(he_secret, kHeSecret1);
  EXPECT_EQ(reset_secret, kResetSecret1);

  EXPECT_TRUE(factory_.GetCryptohomeFrontend()
                  ->SetCurrentUser("obfuscated_username")
                  .ok());
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_EQ(he_secret, kHeSecret1);
  EXPECT_EQ(reset_secret, kResetSecret1);

  EXPECT_TRUE(
      factory_.GetCryptohomeFrontend()->SetCurrentUser("wrong_user").ok());
  EXPECT_EQ(
      LE_CRED_ERROR_PCR_NOT_MATCH,
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
}

// Verify invalid secrets and getting locked out due to too many attempts.
// TODO(pmalani): Update this once we have started modelling the delay schedule
// correctly.
TEST_F(LECredentialManagerImplUnitTest, LockedOutSecret) {
  uint64_t label1 = CreateLockedOutCredential();
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  LECredStatus status =
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret);
  EXPECT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS, status->local_lecred_error());
  EXPECT_TRUE(ContainsActionInStack(status, error::ErrorAction::kTpmLockout));

  // Check once more to ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the
  // right metadata is stored.
  status =
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret);
  EXPECT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS, status->local_lecred_error());
  EXPECT_TRUE(ContainsActionInStack(status, error::ErrorAction::kTpmLockout));
}

// Verify getting locked out due to too many attempts for biometrics
// rate-limiters.
TEST_F(LECredentialManagerImplUnitTest, BiometricsLockedOutRateLimiter) {
  const brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                        std::end(kClientNonceArray));

  GeneratePk(kAuthChannel);
  uint64_t label1 = CreateLockedOutRateLimiter(kAuthChannel);
  auto reply = le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce);
  EXPECT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
            reply.status()->local_lecred_error());
  EXPECT_TRUE(
      ContainsActionInStack(reply.status(), error::ErrorAction::kTpmLockout));

  // Check once more to ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the
  // right metadata is stored.
  reply = le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce);
  EXPECT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
            reply.status()->local_lecred_error());
  EXPECT_TRUE(
      ContainsActionInStack(reply.status(), error::ErrorAction::kTpmLockout));
}

// Insert a label. Then ensure that a CheckCredential on another non-existent
// label fails.
TEST_F(LECredentialManagerImplUnitTest, InvalidLabelCheck) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  uint64_t label1;
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  // First try a badly encoded label.
  uint64_t invalid_label = ~label1;
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_EQ(LE_CRED_ERROR_INVALID_LABEL,
            le_mgr_
                ->CheckCredential(invalid_label, kLeSecret1, &he_secret,
                                  &reset_secret)
                ->local_lecred_error());
  // Next check a valid, but absent label.
  invalid_label = label1 ^ 0x1;
  EXPECT_EQ(LE_CRED_ERROR_INVALID_LABEL,
            le_mgr_
                ->CheckCredential(invalid_label, kLeSecret1, &he_secret,
                                  &reset_secret)
                ->local_lecred_error());
}

// Insert a credential and then remove it.
// Check that a subsequent CheckCredential on that label fails.
TEST_F(LECredentialManagerImplUnitTest, BasicInsertRemove) {
  uint64_t label1;
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  ASSERT_TRUE(le_mgr_->RemoveCredential(label1).ok());
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_EQ(
      LE_CRED_ERROR_INVALID_LABEL,
      le_mgr_->CheckCredential(label1, kHeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
}

// Check that a reset unlocks a locked out credential.
TEST_F(LECredentialManagerImplUnitTest, ResetSecret) {
  uint64_t label1 = CreateLockedOutCredential();
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  ASSERT_EQ(
      LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());

  EXPECT_TRUE(
      le_mgr_->ResetCredential(label1, kResetSecret1, /*strong_reset=*/false)
          .ok());

  he_secret.clear();
  // Make sure we can Check successfully, post reset.
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_EQ(he_secret, kHeSecret1);
}

// Check that an invalid reset doesn't unlock a locked credential.
TEST_F(LECredentialManagerImplUnitTest, ResetSecretNegative) {
  uint64_t label1 = CreateLockedOutCredential();
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  ASSERT_EQ(
      LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());

  EXPECT_EQ(LE_CRED_ERROR_INVALID_RESET_SECRET,
            le_mgr_->ResetCredential(label1, kLeSecret1, /*strong_reset=*/false)
                ->local_lecred_error());

  // Make sure that Check still fails.
  EXPECT_EQ(
      LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
}

// Check that a reset unlocks a locked out rate-limiter.
TEST_F(LECredentialManagerImplUnitTest, BiometricsResetSecret) {
  const brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                        std::end(kClientNonceArray));
  GeneratePk(kAuthChannel);
  uint64_t label1 = CreateLockedOutRateLimiter(kAuthChannel);
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  ASSERT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
            le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce)
                .status()
                ->local_lecred_error());

  EXPECT_TRUE(
      le_mgr_->ResetCredential(label1, kResetSecret1, /*strong_reset=*/false)
          .ok());

  // Make sure we can Check successfully, post reset.
  EXPECT_TRUE(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce).ok());
}

// Check that an invalid reset doesn't unlock a locked rate-limiter.
TEST_F(LECredentialManagerImplUnitTest, BiometricsResetSecretNegative) {
  const brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                        std::end(kClientNonceArray));
  GeneratePk(kAuthChannel);
  uint64_t label1 = CreateLockedOutRateLimiter(kAuthChannel);
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));

  // Ensure that even after an ERROR_TOO_MANY_ATTEMPTS, the right metadata
  // is stored.
  ASSERT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
            le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce)
                .status()
                ->local_lecred_error());

  EXPECT_EQ(LE_CRED_ERROR_INVALID_RESET_SECRET,
            le_mgr_->ResetCredential(label1, kLeSecret1, /*strong_reset=*/false)
                ->local_lecred_error());

  // Make sure that Check still fails.
  EXPECT_EQ(LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
            le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce)
                .status()
                ->local_lecred_error());
}

// Corrupt the hash cache, and see if subsequent LE operations succeed.
// The two cases being tested are removal after corruption, and insertion
// after corruption.
TEST_F(LECredentialManagerImplUnitTest, InsertRemoveCorruptHashCache) {
  uint64_t label1;
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());

  le_mgr_.reset();
  CorruptLeafCache();
  // Now re-initialize the LE Manager.
  InitLEManager();

  // We should be able to regenerate the HashCache.
  EXPECT_TRUE(le_mgr_->RemoveCredential(label1).ok());

  // Now let's reinsert the same credential.
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());

  le_mgr_.reset();
  CorruptLeafCache();
  // Now re-initialize the LE Manager.
  InitLEManager();

  // Let's make sure future operations work.
  uint64_t label2;
  EXPECT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_TRUE(le_mgr_->RemoveCredential(label1).ok());
  EXPECT_TRUE(le_mgr_->RemoveCredential(label2).ok());
}

// Initialize the LECredManager and take a snapshot after 1 operation,
// then perform an insert. Then, restore the snapshot (in effect "losing" the
// last operation). The log functionality should restore the "lost" state.
TEST_F(LECredentialManagerImplUnitTest, LogReplayLostInsert) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());

  base::ScopedTempDir snapshot;
  ASSERT_TRUE(snapshot.CreateUniqueTempDir());
  ASSERT_TRUE(base::CopyDirectory(CredDirPath(), snapshot.GetPath(), true));

  // Another Insert & Remove after taking the snapshot.
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  le_mgr_.reset();
  RestoreSnapshot(snapshot.GetPath());
  InitLEManager();

  // Subsequent operation should work.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
}

// Initialize the LECredManager and take a snapshot after an operation,
// then perform an insert and remove. Then, restore the snapshot
// (in effect "losing" the last 2 operations). The log functionality
// should restore the "lost" state.
TEST_F(LECredentialManagerImplUnitTest, LogReplayLostInsertRemove) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Another Insert & Remove after taking the snapshot.
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());
  ASSERT_TRUE(le_mgr_->RemoveCredential(label1).ok());

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Subsequent operation should work.
  uint64_t label3;
  EXPECT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label3)
          .ok());
}

// Initialize the LECredManager and take a snapshot after 2 operations,
// then perform |kLogSize| checks. Then, restore the snapshot (in effect
// "losing" the last |kLogSize| operations). The log functionality should
// restore the "lost" state.
TEST_F(LECredentialManagerImplUnitTest, LogReplayLostChecks) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kLeSecret2(std::begin(kLeSecret2Array),
                                std::end(kLeSecret2Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Perform incorrect checks to fill up the replay log.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  for (int i = 0; i < kFakeLogSize; i++) {
    ASSERT_EQ(
        LE_CRED_ERROR_INVALID_LE_SECRET,
        le_mgr_->CheckCredential(label1, kLeSecret2, &he_secret, &reset_secret)
            ->local_lecred_error());
  }

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Subsequent operations should work.
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label2, kLeSecret2, &he_secret, &reset_secret)
          .ok());
}

// Initialize the LECredManager and take a snapshot after 2 operations,
// then perform |kLogSize| inserts. Then, restore the snapshot (in effect
// "losing" the last |kLogSize| operations). The log functionality should
// restore the "lost" state.
TEST_F(LECredentialManagerImplUnitTest, LogReplayLostInserts) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kLeSecret2(std::begin(kLeSecret2Array),
                                std::end(kLeSecret2Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Perform inserts to fill up the replay log.
  uint64_t temp_label;
  for (int i = 0; i < kFakeLogSize; i++) {
    ASSERT_TRUE(le_mgr_
                    ->InsertCredential(
                        std::vector<hwsec::OperationPolicySetting>(),
                        kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                        /*expiration_delay=*/std::nullopt, &temp_label)
                    .ok());
  }

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Subsequent operations should work.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label2, kLeSecret2, &he_secret, &reset_secret)
          .ok());
  EXPECT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &temp_label)
          .ok());
  EXPECT_TRUE(le_mgr_->RemoveCredential(label1).ok());
}

// Initialize the LECredManager, insert 2 base credentials. Then, insert
// |kLogSize| credentials. Then, take a snapshot, and then remove the
// |kLogSize| credentials. Then, restore the snapshot (in effect "losing" the
// last |kLogSize| operations). The log functionality should restore the "lost"
// state.
TEST_F(LECredentialManagerImplUnitTest, LogReplayLostRemoves) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kLeSecret2(std::begin(kLeSecret2Array),
                                std::end(kLeSecret2Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  // Perform |kLogSize| credential inserts.
  std::vector<uint64_t> labels_to_remove;
  uint64_t temp_label;
  for (int i = 0; i < kFakeLogSize; i++) {
    ASSERT_TRUE(le_mgr_
                    ->InsertCredential(
                        std::vector<hwsec::OperationPolicySetting>(),
                        kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                        /*expiration_delay=*/std::nullopt, &temp_label)
                    .ok());
    labels_to_remove.push_back(temp_label);
  }

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Fill the replay log with |kLogSize| RemoveCredential operations.
  for (int i = 0; i < kFakeLogSize; i++) {
    ASSERT_TRUE(le_mgr_->RemoveCredential(labels_to_remove[i]).ok());
  }

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Verify that the removed credentials are actually gone.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  for (int i = 0; i < kFakeLogSize; i++) {
    EXPECT_EQ(LE_CRED_ERROR_INVALID_LABEL,
              le_mgr_
                  ->CheckCredential(labels_to_remove[i], kLeSecret1, &he_secret,
                                    &reset_secret)
                  ->local_lecred_error());
  }

  // Subsequent operations should work.
  he_secret.clear();
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());
  EXPECT_TRUE(
      le_mgr_->CheckCredential(label2, kLeSecret2, &he_secret, &reset_secret)
          .ok());
  EXPECT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &temp_label)
          .ok());
  EXPECT_TRUE(le_mgr_->RemoveCredential(label1).ok());
}

// Initialize the LECredManager and take a snapshot after 2 operations,
// then perform |kLogSize| inserts of rate-limiters. Then, restore the snapshot
// (in effect "losing" the last |kLogSize| operations). The log functionality
// should restore the "lost" state.
TEST_F(LECredentialManagerImplUnitTest, BiometricsLogReplayLostInserts) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));
  brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                  std::end(kClientNonceArray));

  GeneratePk(kAuthChannel);
  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Perform inserts to fill up the replay log.
  uint64_t temp_label;
  for (int i = 0; i < kFakeLogSize; i++) {
    ASSERT_TRUE(
        le_mgr_
            ->InsertRateLimiter(kAuthChannel,
                                std::vector<hwsec::OperationPolicySetting>(),
                                kResetSecret1, delay_sched,
                                /*expiration_delay=*/std::nullopt, &temp_label)
            .ok());
  }

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Subsequent operations should work.
  ASSERT_TRUE(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce).ok());
  ASSERT_TRUE(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label2, kClientNonce).ok());
  EXPECT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &temp_label)
          .ok());
  EXPECT_TRUE(le_mgr_->RemoveCredential(label1).ok());
}

// Initialize the LECredManager and take a snapshot after 2 operations,
// then perform |kLogSize| start auths of rate-limiters. Then, restore the
// snapshot (in effect "losing" the last |kLogSize| operations). The log
// functionality should restore the "lost" state.
TEST_F(LECredentialManagerImplUnitTest, BiometricsLogReplayLostStartAuths) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));
  brillo::SecureBlob kClientNonce(std::begin(kClientNonceArray),
                                  std::end(kClientNonceArray));

  GeneratePk(kAuthChannel);
  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertRateLimiter(kAuthChannel,
                              std::vector<hwsec::OperationPolicySetting>(),
                              kResetSecret1, delay_sched,
                              /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Perform start auths to fill up the replay log.
  for (int i = 0; i < kFakeLogSize; i++) {
    ASSERT_TRUE(
        le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce).ok());
  }

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Subsequent operations should work.
  ASSERT_TRUE(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label1, kClientNonce).ok());
  ASSERT_TRUE(
      le_mgr_->StartBiometricsAuth(kAuthChannel, label2, kClientNonce).ok());
}

// Verify behaviour when more operations are lost than the log can save.
// NOTE: The number of lost operations should always be greater than
// the log size of pinweaver.
TEST_F(LECredentialManagerImplUnitTest, FailedLogReplayTooManyOps) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kLeSecret2(std::begin(kLeSecret2Array),
                                std::end(kLeSecret2Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  // Perform insert.
  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());

  std::unique_ptr<base::ScopedTempDir> snapshot = CaptureSnapshot();

  // Perform |kFakeLogSize| + 1 incorrect checks and an insert.
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  for (int i = 0; i < kFakeLogSize + 1; i++) {
    ASSERT_EQ(
        LE_CRED_ERROR_INVALID_LE_SECRET,
        le_mgr_->CheckCredential(label1, kLeSecret2, &he_secret, &reset_secret)
            ->local_lecred_error());
  }
  uint64_t label3;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label3)
          .ok());

  le_mgr_.reset();
  RestoreSnapshot(snapshot->GetPath());
  InitLEManager();

  // Subsequent operations should fail.
  // TODO(crbug.com/809710): Should we reset the tree in this case?
  EXPECT_EQ(
      LE_CRED_ERROR_HASH_TREE,
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
  EXPECT_EQ(
      LE_CRED_ERROR_HASH_TREE,
      le_mgr_->CheckCredential(label2, kLeSecret2, &he_secret, &reset_secret)
          ->local_lecred_error());
}

// Verify behaviour when there is an unsalvageable disk corruption.
TEST_F(LECredentialManagerImplUnitTest, FailedSyncDiskCorrupted) {
  std::map<uint32_t, uint32_t> delay_sched = {
      {kLEMaxIncorrectAttempt, UINT32_MAX},
  };
  brillo::SecureBlob kLeSecret1(std::begin(kLeSecret1Array),
                                std::end(kLeSecret1Array));
  brillo::SecureBlob kLeSecret2(std::begin(kLeSecret2Array),
                                std::end(kLeSecret2Array));
  brillo::SecureBlob kHeSecret1(std::begin(kHeSecret1Array),
                                std::end(kHeSecret1Array));
  brillo::SecureBlob kResetSecret1(std::begin(kResetSecret1Array),
                                   std::end(kResetSecret1Array));

  uint64_t label1;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label1)
          .ok());
  uint64_t label2;
  ASSERT_TRUE(
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret1, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          .ok());
  brillo::SecureBlob he_secret;
  brillo::SecureBlob reset_secret;
  ASSERT_TRUE(
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          .ok());

  // Corrupt the content of two label folders and the cache file.
  le_mgr_.reset();
  CorruptHashTreeWithLabel(label1);
  CorruptHashTreeWithLabel(label2);
  CorruptLeafCache();

  // Now re-initialize the LE Manager.
  InitLEManager();

  // Any operation should now fail.
  // TODO(crbug.com/809710): Should we reset the tree in this case?
  he_secret.clear();
  EXPECT_EQ(
      LE_CRED_ERROR_HASH_TREE,
      le_mgr_->CheckCredential(label1, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
  EXPECT_EQ(
      LE_CRED_ERROR_HASH_TREE,
      le_mgr_->CheckCredential(label2, kLeSecret1, &he_secret, &reset_secret)
          ->local_lecred_error());
  EXPECT_EQ(
      LE_CRED_ERROR_HASH_TREE,
      le_mgr_
          ->InsertCredential(std::vector<hwsec::OperationPolicySetting>(),
                             kLeSecret2, kHeSecret1, kResetSecret1, delay_sched,
                             /*expiration_delay=*/std::nullopt, &label2)
          ->local_lecred_error());
}

}  // namespace cryptohome
