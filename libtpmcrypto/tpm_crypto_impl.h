// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTPMCRYPTO_TPM_CRYPTO_IMPL_H_
#define LIBTPMCRYPTO_TPM_CRYPTO_IMPL_H_

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include <brillo/brillo_export.h>

#include "libtpmcrypto/tpm.h"
#include "libtpmcrypto/tpm_crypto.h"

namespace tpmcrypto {

class BRILLO_EXPORT TpmCryptoImpl : public TpmCrypto {
 public:
  using RandBytesFn = std::function<int(uint8_t*, int)>;

  TpmCryptoImpl();
  explicit TpmCryptoImpl(std::unique_ptr<Tpm> tpm);
  TpmCryptoImpl(std::unique_ptr<Tpm> tpm, RandBytesFn rand_bytes_fn);
  TpmCryptoImpl(const TpmCryptoImpl&) = delete;
  TpmCryptoImpl& operator=(const TpmCryptoImpl&) = delete;

  ~TpmCryptoImpl() override;

  [[nodiscard]] bool Encrypt(const brillo::SecureBlob& data,
                             std::string* encrypted_data) override;

  [[nodiscard]] bool Decrypt(const std::string& encrypted_data,
                             brillo::SecureBlob* data) override;

 private:
  // Creates a randomly generated aes key and seals it to the TPM's PCR0.
  [[nodiscard]] bool CreateSealedKey(brillo::SecureBlob* aes_key,
                                     brillo::SecureBlob* sealed_key) const;

  // Encrypts the given data using the aes_key. Sealed key is necessary to
  // wrap into the returned data to allow for decryption.
  [[nodiscard]] bool EncryptData(const brillo::SecureBlob& data,
                                 const brillo::SecureBlob& aes_key,
                                 const brillo::SecureBlob& sealed_key,
                                 std::string* encrypted_data) const;

  // Gets random bytes and returns them in a SecureBlob.
  [[nodiscard]] bool GetRandomDataSecureBlob(size_t length,
                                             brillo::SecureBlob* data) const;

  // The TPM implementation
  std::unique_ptr<Tpm> tpm_;
  RandBytesFn rand_bytes_fn_;
};

}  // namespace tpmcrypto

#endif  // LIBTPMCRYPTO_TPM_CRYPTO_IMPL_H_
