// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_TPM_AUTH_BLOCK_UTILS_H_
#define CRYPTOHOME_AUTH_BLOCKS_TPM_AUTH_BLOCK_UTILS_H_

#include <string>

#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmAuthBlockUtils {
 public:
  TpmAuthBlockUtils(hwsec::CryptohomeFrontend* hwsec,
                    CryptohomeKeyLoader* cryptohome_key_loader);
  TpmAuthBlockUtils(const TpmAuthBlockUtils&) = delete;
  TpmAuthBlockUtils& operator=(const TpmAuthBlockUtils&) = delete;

  // A static method that converts a TPMRetryAction into CryptoError.
  static CryptoError TPMRetryActionToCrypto(const hwsec::TPMRetryAction retry);

  // A static method that converts an error object.
  // |err| shouldn't be a nullptr.
  static CryptoError TPMErrorToCrypto(const hwsec::Status& err);

  // A static method that converts a TPM error into CryptohomeCryptoError.
  static CryptoStatus TPMErrorToCryptohomeCryptoError(hwsec::Status err);

  // A static method to report which errors can be recovered from with a retry.
  // |err| shouldn't be a nullptr.
  static bool TPMErrorIsRetriable(const hwsec::Status& err);

  // Checks if the specified |hash| is the same as the hash for the |tpm_| used
  // by the class.
  CryptoStatus IsTPMPubkeyHash(const brillo::SecureBlob& hash) const;

  // This checks that the TPM is ready and that the vault keyset was encrypted
  // with this machine's TPM.
  CryptoStatus CheckTPMReadiness(bool has_tpm_key,
                                 bool has_tpm_public_key_hash,
                                 const brillo::SecureBlob& tpm_public_key_hash);

 private:
  hwsec::CryptohomeFrontend* hwsec_;
  CryptohomeKeyLoader* cryptohome_key_loader_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_TPM_AUTH_BLOCK_UTILS_H_
