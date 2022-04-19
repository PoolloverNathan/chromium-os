// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_IMPL_H_

#include <string>

#include <brillo/secure_blob.h>

#include "libhwsec/frontend/cryptohome/frontend.h"
#include "libhwsec/frontend/frontend_impl.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace hwsec {

class HWSEC_EXPORT CryptohomeFrontendImpl : public CryptohomeFrontend,
                                            public FrontendImpl {
 public:
  using FrontendImpl::FrontendImpl;
  ~CryptohomeFrontendImpl() override = default;

  StatusOr<bool> IsEnabled() override;
  StatusOr<bool> IsReady() override;
  StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() override;
  StatusOr<CreateKeyResult> CreateCryptohomeKey(KeyAlgoType key_algo) override;
  StatusOr<ScopedKey> LoadKey(const brillo::Blob& key_blob) override;
  StatusOr<brillo::Blob> GetPubkeyHash(Key key) override;
  StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) override;
  StatusOr<uint32_t> GetKeyHandle(Key key) override;
  Status SetCurrentUser(const std::string& current_user) override;
  StatusOr<brillo::Blob> SealWithCurrentUser(
      const std::optional<std::string>& current_user,
      const brillo::SecureBlob& auth_value,
      const brillo::SecureBlob& unsealed_data) override;
  StatusOr<std::optional<ScopedKey>> PreloadSealedData(
      const brillo::Blob& sealed_data) override;
  StatusOr<brillo::SecureBlob> UnsealWithCurrentUser(
      std::optional<Key> preload_data,
      const brillo::SecureBlob& auth_value,
      const brillo::Blob& sealed_data) override;
  StatusOr<brillo::Blob> Encrypt(Key key,
                                 const brillo::SecureBlob& plaintext) override;
  StatusOr<brillo::SecureBlob> Decrypt(Key key,
                                       const brillo::Blob& ciphertext) override;
  StatusOr<brillo::SecureBlob> GetAuthValue(
      Key key, const brillo::SecureBlob& pass_blob) override;
  StatusOr<brillo::Blob> GetRandomBlob(size_t size) override;
  StatusOr<brillo::SecureBlob> GetRandomSecureBlob(size_t size) override;
  StatusOr<uint32_t> GetManufacturer() override;
  StatusOr<bool> IsPinWeaverEnabled() override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_CRYPTOHOME_FRONTEND_IMPL_H_
