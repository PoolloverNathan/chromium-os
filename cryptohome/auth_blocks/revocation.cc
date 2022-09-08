// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/revocation.h"

#include <map>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hkdf.h>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager.h"

using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::Hkdf;
using ::hwsec_foundation::HkdfHash;
using ::hwsec_foundation::kDefaultAesKeySize;

namespace cryptohome {
namespace revocation {

namespace {
constexpr int kDefaultSecretSize = 32;
// String used as vector in HKDF operation to derive vkk_key from he_secret.
const char kHESecretHkdfData[] = "hkdf_data";
// String used as info in HKDF operation to derive le_secret from
// per_credential_secret.
const char kLeSecretInfo[] = "le_secret_info";
// String used as info in HKDF operation to derive kdf_skey from
// per_credential_secret.
const char kKdfSkeyInfo[] = "kdf_skey_info";

// The format for a delay schedule entry is as follows:
// (number_of_incorrect_attempts, delay before_next_attempt)
// The delay is not needed for revocation, so we set
// number_of_incorrect_attempts to UINT32_MAX.
LECredentialManager::DelaySchedule GetDelaySchedule() {
  return std::map<uint32_t, uint32_t>{{UINT32_MAX, 1}};
}

CryptoError RevokeLECredErrorToCryptoError(LECredError le_error) {
  switch (le_error) {
    case LE_CRED_ERROR_INVALID_LABEL:
    case LE_CRED_ERROR_HASH_TREE:
      // Do not return an error here. RemoveCredential returns:
      // - LE_CRED_ERROR_INVALID_LABEL for invalid label.
      // - LE_CRED_ERROR_HASH_TREE for hash tree error (implies that all state
      // in PinWeaver is lost). Both of these cases are considered as "success"
      // for revocation.
      return CryptoError::CE_NONE;
    case LE_CRED_ERROR_UNCLASSIFIED:
    case LE_CRED_ERROR_LE_LOCKED:
      return CryptoError::CE_OTHER_CRYPTO;
    default:
      return CryptoError::CE_OTHER_CRYPTO;
  }
}

bool DeriveSecret(const brillo::SecureBlob& key,
                  const brillo::SecureBlob& hkdf_info,
                  brillo::SecureBlob* gen_secret) {
  // Note: the key is high entropy, so the salt can be empty.
  if (!Hkdf(HkdfHash::kSha256, /*key=*/key,
            /*info=*/hkdf_info,
            /*salt=*/brillo::SecureBlob(),
            /*result_len=*/gen_secret->size(), gen_secret)) {
    LOG(ERROR) << "HKDF failed during secret derivation.";
    return false;
  }
  return true;
}

}  // namespace

bool IsRevocationSupported(hwsec::CryptohomeFrontend* hwsec) {
  hwsec::StatusOr<bool> enabled = hwsec->IsPinWeaverEnabled();
  return enabled.ok() && *enabled;
}

CryptoError Create(LECredentialManager* le_manager,
                   RevocationState* revocation_state,
                   KeyBlobs* key_blobs) {
  DCHECK(le_manager);
  if (!key_blobs->vkk_key.has_value() || key_blobs->vkk_key.value().empty()) {
    LOG(ERROR) << "Failed to create secret: vkk_key is not set";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // The secret generated by AuthBlock.
  brillo::SecureBlob per_credential_secret = key_blobs->vkk_key.value();

  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  // Derive two secrets from per_credential_secret:
  // le_secret to be stored in LECredentialManager,
  // kdf_skey to be combined with he_secret for vkk_key generation.
  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  if (!DeriveSecret(per_credential_secret, brillo::SecureBlob(kLeSecretInfo),
                    &le_secret) ||
      !DeriveSecret(per_credential_secret, brillo::SecureBlob(kKdfSkeyInfo),
                    &kdf_skey)) {
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // Generate a random high entropy secret to be stored in LECredentialManager.
  brillo::SecureBlob he_secret = CreateSecureRandomBlob(kDefaultSecretSize);

  // Label for the credential stored in LECredentialManager.
  uint64_t label;

  // Note:
  // - We send an empty blob as reset_secret because resetting the delay counter
  // will not compromise security (we send MAX_UINT32 attempts for the delay
  // schedule). The size should still be kDefaultSecretSize.
  // - We don't set policies because PCR binding is expected to be
  // already done by the AuthBlock.
  LECredStatus ret = le_manager->InsertCredential(
      /*policy=*/std::vector<hwsec::OperationPolicySetting>(),
      /*le_secret=*/le_secret,
      /*he_secret=*/he_secret,
      /*reset_secret=*/brillo::SecureBlob(kDefaultSecretSize),
      /*delay_sched=*/GetDelaySchedule(), &label);

  if (!ret.ok())
    return ret->local_crypto_error();

  revocation_state->le_label = label;

  // Combine he_secret with kdf_skey:
  brillo::SecureBlob vkk_key;
  if (!Hkdf(HkdfHash::kSha256,
            /*key=*/brillo::SecureBlob::Combine(he_secret, kdf_skey),
            /*info=*/brillo::SecureBlob(),
            /*salt=*/brillo::SecureBlob(kHESecretHkdfData),
            /*result_len=*/0, &vkk_key)) {
    LOG(ERROR) << "vkk_key HKDF derivation failed";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  key_blobs->vkk_key = std::move(vkk_key);

  return CryptoError::CE_NONE;
}

CryptoError Derive(LECredentialManager* le_manager,
                   const RevocationState& revocation_state,
                   KeyBlobs* key_blobs) {
  DCHECK(le_manager);
  if (!key_blobs->vkk_key.has_value() || key_blobs->vkk_key.value().empty()) {
    LOG(ERROR) << "Failed to derive secret: vkk_key is not set";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  if (!revocation_state.le_label.has_value()) {
    LOG(ERROR)
        << "Failed to derive secret: revocation_state.le_label is not set";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // The secret generated by AuthBlock.
  brillo::SecureBlob per_credential_secret = key_blobs->vkk_key.value();
  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  if (!DeriveSecret(per_credential_secret, brillo::SecureBlob(kLeSecretInfo),
                    &le_secret) ||
      !DeriveSecret(per_credential_secret, brillo::SecureBlob(kKdfSkeyInfo),
                    &kdf_skey)) {
    return CryptoError::CE_OTHER_CRYPTO;
  }

  // The secret that is stored in LECredentialManager.
  brillo::SecureBlob he_secret;
  // Note: reset_secret is not used, see Create().
  brillo::SecureBlob reset_secret;
  LECredStatus ret = le_manager->CheckCredential(
      /*label=*/revocation_state.le_label.value(),
      /*le_secret=*/le_secret,
      /*he_secret=*/&he_secret,
      /*reset_secret=*/&reset_secret);

  if (!ret.ok())
    return ret->local_crypto_error();

  // Combine he_secret with kdf_skey:
  brillo::SecureBlob vkk_key;
  if (!Hkdf(HkdfHash::kSha256,
            /*key=*/brillo::SecureBlob::Combine(he_secret, kdf_skey),
            /*info=*/brillo::SecureBlob(),
            /*salt=*/brillo::SecureBlob(kHESecretHkdfData),
            /*result_len=*/0, &vkk_key)) {
    LOG(ERROR) << "vkk_key HKDF derivation failed";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  key_blobs->vkk_key = std::move(vkk_key);
  return CryptoError::CE_NONE;
}

CryptoError Revoke(AuthBlockType auth_block_type,
                   LECredentialManager* le_manager,
                   const RevocationState& revocation_state) {
  DCHECK(le_manager);
  if (!revocation_state.le_label.has_value()) {
    LOG(ERROR)
        << "Failed to revoke secret: revocation_state.le_label is not set";
    return CryptoError::CE_OTHER_CRYPTO;
  }

  LECredStatus ret = le_manager->RemoveCredential(
      /*label=*/revocation_state.le_label.value());

  if (!ret.ok()) {
    LOG(ERROR) << "RemoveCredential failed with error: " << ret;
    ReportCredentialRevocationResult(auth_block_type,
                                     ret->local_lecred_error());
    return RevokeLECredErrorToCryptoError(ret->local_lecred_error());
  }

  ReportCredentialRevocationResult(auth_block_type,
                                   LECredError::LE_CRED_SUCCESS);
  return CryptoError::CE_NONE;
}

}  // namespace revocation
}  // namespace cryptohome
