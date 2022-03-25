// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/signature_sealing_backend_tpm2_impl.h"

#include <cstring>
#include <optional>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/numerics/safe_conversions.h>
#include <base/threading/thread_checker.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/key.pb.h>
#include <google/protobuf/repeated_field.h>
#include <libhwsec/error/tpm2_error.h>
#include <trunks/error_codes.h>
#include <trunks/policy_session.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory_impl.h>
#include <trunks/hmac_session.h>
#include <trunks/tpm_utility.h>
#include <trunks/authorization_delegate.h>

#include "cryptohome/signature_sealed_data.pb.h"
#include "cryptohome/tpm2_impl.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::CombineBlobs;
using brillo::SecureBlob;
using hwsec::TPM2Error;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::error::CreateError;
using hwsec_foundation::error::WrapError;
using hwsec_foundation::status::StatusChain;
using trunks::GetErrorString;
using trunks::TPM_ALG_ID;
using trunks::TPM_ALG_NULL;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;

namespace cryptohome {

namespace {

// Size, in bytes, of the secret value that is generated by
// SignatureSealingBackendTpm2Impl::CreateSealedSecret().
constexpr int kSecretSizeBytes = 32;

class UnsealingSessionTpm2Impl final
    : public SignatureSealingBackend::UnsealingSession {
 public:
  UnsealingSessionTpm2Impl(
      Tpm2Impl* tpm,
      Tpm2Impl::TrunksClientContext* trunks,
      const Blob& srk_wrapped_secret,
      const Blob& public_key_spki_der,
      structure::ChallengeSignatureAlgorithm algorithm,
      TPM_ALG_ID scheme,
      TPM_ALG_ID hash_alg,
      std::unique_ptr<trunks::PolicySession> policy_session,
      const Blob& policy_session_tpm_nonce);
  UnsealingSessionTpm2Impl(const UnsealingSessionTpm2Impl&) = delete;
  UnsealingSessionTpm2Impl& operator=(const UnsealingSessionTpm2Impl&) = delete;

  ~UnsealingSessionTpm2Impl() override;

  // UnsealingSession:
  structure::ChallengeSignatureAlgorithm GetChallengeAlgorithm() override;
  Blob GetChallengeValue() override;
  StatusChain<TPMErrorBase> Unseal(const Blob& signed_challenge_value,
                                   SecureBlob* unsealed_value) override;

 private:
  // Unowned.
  Tpm2Impl* const tpm_;
  // Unowned.
  Tpm2Impl::TrunksClientContext* const trunks_;
  const Blob srk_wrapped_secret_;
  const Blob public_key_spki_der_;
  const structure::ChallengeSignatureAlgorithm algorithm_;
  const TPM_ALG_ID scheme_;
  const TPM_ALG_ID hash_alg_;
  const std::unique_ptr<trunks::PolicySession> policy_session_;
  const Blob policy_session_tpm_nonce_;
  base::ThreadChecker thread_checker_;
};

// Obtains the TPM 2.0 signature scheme and hashing algorithms that correspond
// to the provided challenge signature algorithm.
bool GetAlgIdsByAlgorithm(structure::ChallengeSignatureAlgorithm algorithm,
                          TPM_ALG_ID* scheme,
                          TPM_ALG_ID* hash_alg) {
  switch (algorithm) {
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA1;
      return true;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA256;
      return true;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA384;
      return true;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA512;
      return true;
  }
  NOTREACHED();
  return false;
}

UnsealingSessionTpm2Impl::UnsealingSessionTpm2Impl(
    Tpm2Impl* tpm,
    Tpm2Impl::TrunksClientContext* trunks,
    const Blob& srk_wrapped_secret,
    const Blob& public_key_spki_der,
    structure::ChallengeSignatureAlgorithm algorithm,
    TPM_ALG_ID scheme,
    TPM_ALG_ID hash_alg,
    std::unique_ptr<trunks::PolicySession> policy_session,
    const Blob& policy_session_tpm_nonce)
    : tpm_(tpm),
      trunks_(trunks),
      srk_wrapped_secret_(srk_wrapped_secret),
      public_key_spki_der_(public_key_spki_der),
      algorithm_(algorithm),
      scheme_(scheme),
      hash_alg_(hash_alg),
      policy_session_(std::move(policy_session)),
      policy_session_tpm_nonce_(policy_session_tpm_nonce) {}

UnsealingSessionTpm2Impl::~UnsealingSessionTpm2Impl() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

structure::ChallengeSignatureAlgorithm
UnsealingSessionTpm2Impl::GetChallengeAlgorithm() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return algorithm_;
}

Blob UnsealingSessionTpm2Impl::GetChallengeValue() {
  DCHECK(thread_checker_.CalledOnValidThread());
  const Blob expiration_blob(4);  // zero expiration (4-byte integer)
  return CombineBlobs({policy_session_tpm_nonce_, expiration_blob});
}

StatusChain<TPMErrorBase> UnsealingSessionTpm2Impl::Unseal(
    const Blob& signed_challenge_value, SecureBlob* unsealed_value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Start a TPM authorization session.
  std::unique_ptr<trunks::HmacSession> session =
      trunks_->factory->GetHmacSession();
  if (auto err = CreateError<TPM2Error>(
          trunks_->tpm_utility->StartSession(session.get()))) {
    return WrapError<TPMError>(std::move(err), "Error starting hmac session");
  }
  // Load the protection public key onto the TPM.
  ScopedKeyHandle key_handle;
  if (!tpm_->LoadPublicKeyFromSpki(
          public_key_spki_der_, AsymmetricKeyUsage::kSignKey, scheme_,
          hash_alg_, session->GetDelegate(), &key_handle)) {
    return CreateError<TPMError>("Error loading protection key",
                                 TPMRetryAction::kNoRetry);
  }
  std::string key_name;
  if (auto err = CreateError<TPM2Error>(
          trunks_->tpm_utility->GetKeyName(key_handle.value(), &key_name))) {
    return WrapError<TPMError>(std::move(err), "Failed to get key name");
  }
  // Update the policy with the signature.
  trunks::TPMT_SIGNATURE signature;
  memset(&signature, 0, sizeof(trunks::TPMT_SIGNATURE));
  signature.sig_alg = scheme_;
  signature.signature.rsassa.hash = hash_alg_;
  signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA(BlobToString(signed_challenge_value));
  if (auto err = CreateError<TPM2Error>(policy_session_->PolicySigned(
          key_handle.value(), key_name, BlobToString(policy_session_tpm_nonce_),
          std::string() /* cp_hash */, std::string() /* policy_ref */,
          0 /* expiration */, signature, session->GetDelegate()))) {
    return WrapError<TPMError>(
        std::move(err),
        "Error restricting policy to signature with the public key");
  }
  // Obtain the resulting policy digest.
  std::string policy_digest;
  if (auto err =
          CreateError<TPM2Error>(policy_session_->GetDigest(&policy_digest))) {
    return WrapError<TPMError>(std::move(err), "Error getting policy digest");
  }
  // Unseal the secret value.
  std::string unsealed_value_string;
  if (auto err = CreateError<TPM2Error>(trunks_->tpm_utility->UnsealData(
          BlobToString(srk_wrapped_secret_), policy_session_->GetDelegate(),
          &unsealed_value_string))) {
    return WrapError<TPMError>(std::move(err), "Error unsealing object");
  }
  *unsealed_value = SecureBlob(unsealed_value_string);
  return nullptr;
}

std::map<uint32_t, std::string> ToStrPcrMap(
    const std::map<uint32_t, brillo::Blob>& pcr_map) {
  std::map<uint32_t, std::string> str_pcr_map;
  for (const auto& [index, value] : pcr_map) {
    str_pcr_map[index] = brillo::BlobToString(value);
  }
  return str_pcr_map;
}

StatusChain<TPMError> GetPcrPolicyDigest(
    trunks::PolicySession* policy_session,
    const std::map<uint32_t, brillo::Blob>& pcr_map,
    std::string* pcr_policy_digest) {
  std::map<uint32_t, std::string> str_pcr_map = ToStrPcrMap(pcr_map);

  // Run PolicyPCR against the PCR set.
  if (auto err =
          CreateError<TPM2Error>(policy_session->PolicyPCR(str_pcr_map))) {
    return WrapError<TPMError>(std::move(err),
                               "Error restricting policy to PCRs");
  }
  // Remember the policy digest for the PCR set.
  if (auto err = CreateError<TPM2Error>(
          policy_session->GetDigest(pcr_policy_digest))) {
    return WrapError<TPMError>(std::move(err), "Error getting policy digest");
  }

  // Restart the policy session.
  if (auto err = CreateError<TPM2Error>(policy_session->PolicyRestart())) {
    return WrapError<TPMError>(std::move(err),
                               "Error restarting the policy session");
  }
  return nullptr;
}

}  // namespace

SignatureSealingBackendTpm2Impl::SignatureSealingBackendTpm2Impl(Tpm2Impl* tpm)
    : tpm_(tpm) {}

SignatureSealingBackendTpm2Impl::~SignatureSealingBackendTpm2Impl() = default;

StatusChain<TPMErrorBase> SignatureSealingBackendTpm2Impl::CreateSealedSecret(
    const Blob& public_key_spki_der,
    const std::vector<structure::ChallengeSignatureAlgorithm>& key_algorithms,
    const std::map<uint32_t, brillo::Blob>& default_pcr_map,
    const std::map<uint32_t, brillo::Blob>& extended_pcr_map,
    const Blob& /* delegate_blob */,
    const Blob& /* delegate_secret */,
    SecureBlob* secret_value,
    structure::SignatureSealedData* sealed_secret_data) {
  // Choose the algorithm. Respect the input's algorithm prioritization, with
  // the exception of considering SHA-1 as the least preferred option.
  TPM_ALG_ID scheme = TPM_ALG_NULL;
  TPM_ALG_ID hash_alg = TPM_ALG_NULL;
  for (auto algorithm : key_algorithms) {
    TPM_ALG_ID current_scheme = TPM_ALG_NULL;
    TPM_ALG_ID current_hash_alg = TPM_ALG_NULL;
    if (GetAlgIdsByAlgorithm(algorithm, &current_scheme, &current_hash_alg)) {
      scheme = current_scheme;
      hash_alg = current_hash_alg;
      if (hash_alg != trunks::TPM_ALG_SHA1)
        break;
    }
  }
  if (scheme == TPM_ALG_NULL) {
    return CreateError<TPMError>("Error choosing the signature algorithm",
                                 TPMRetryAction::kNoRetry);
  }
  // Start a TPM authorization session.
  Tpm2Impl::TrunksClientContext* trunks = nullptr;
  if (!tpm_->GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  std::unique_ptr<trunks::HmacSession> session =
      trunks->factory->GetHmacSession();
  if (auto err = CreateError<TPM2Error>(
          trunks->tpm_utility->StartSession(session.get()))) {
    return WrapError<TPMError>(std::move(err), "Error starting hmac session");
  }
  // Load the protection public key onto the TPM.
  ScopedKeyHandle key_handle;
  if (!tpm_->LoadPublicKeyFromSpki(
          public_key_spki_der, AsymmetricKeyUsage::kSignKey, scheme, hash_alg,
          session->GetDelegate(), &key_handle)) {
    return CreateError<TPMError>("Error loading protection key",
                                 TPMRetryAction::kNoRetry);
  }
  std::string key_name;
  if (auto err = CreateError<TPM2Error>(
          trunks->tpm_utility->GetKeyName(key_handle.value(), &key_name))) {
    return WrapError<TPMError>(std::move(err), "Failed to get key name");
  }
  // Start a trial policy session for sealing the secret value.
  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetTrialSession();
  if (auto err = CreateError<TPM2Error>(
          policy_session->StartUnboundSession(true, false))) {
    return WrapError<TPMError>(std::move(err),
                               "Error starting a trial session");
  }

  // Calculate policy digests for each of the sets of PCR restrictions
  // separately. Rewind each time the policy session back to the initial state.
  std::vector<std::string> pcr_policy_digests;

  std::string default_pcr_policy_digest;
  if (StatusChain<TPMError> err = GetPcrPolicyDigest(
          policy_session.get(), default_pcr_map, &default_pcr_policy_digest)) {
    return WrapError<TPMError>(std::move(err),
                               "Error getting default PCR policy digest");
  }
  pcr_policy_digests.push_back(default_pcr_policy_digest);

  std::string extended_pcr_policy_digest;
  if (StatusChain<TPMError> err = GetPcrPolicyDigest(
          policy_session.get(), default_pcr_map, &extended_pcr_policy_digest)) {
    return WrapError<TPMError>(std::move(err),
                               "Error getting default PCR policy digest");
  }
  pcr_policy_digests.push_back(extended_pcr_policy_digest);

  // Apply PolicyOR for restricting to the disjunction of the specified sets of
  // PCR restrictions.
  if (auto err = CreateError<TPM2Error>(
          policy_session->PolicyOR(pcr_policy_digests))) {
    return WrapError<TPMError>(
        std::move(err),
        "Error restricting policy to logical disjunction of PCRs");
  }

  // Update the policy with an empty signature that refers to the public key.
  trunks::TPMT_SIGNATURE signature;
  memset(&signature, 0, sizeof(trunks::TPMT_SIGNATURE));
  signature.sig_alg = scheme;
  signature.signature.rsassa.hash = hash_alg;
  signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA(std::string());
  if (auto err = CreateError<TPM2Error>(policy_session->PolicySigned(
          key_handle.value(), key_name, std::string() /* nonce */,
          std::string() /* cp_hash */, std::string() /* policy_ref */,
          0 /* expiration */, signature, session->GetDelegate()))) {
    return WrapError<TPMError>(
        std::move(err),
        "Error restricting policy to signature with the public key");
  }
  // Obtain the resulting policy digest.
  std::string policy_digest;
  if (auto err =
          CreateError<TPM2Error>(policy_session->GetDigest(&policy_digest))) {
    return WrapError<TPMError>(std::move(err), "Error getting policy digest");
  }
  if (policy_digest.size() != SHA256_DIGEST_SIZE) {
    return CreateError<TPMError>("Unexpected policy digest size",
                                 TPMRetryAction::kNoRetry);
  }
  // Generate the secret value randomly.
  if (auto err =
          tpm_->GetRandomDataSecureBlob(kSecretSizeBytes, secret_value)) {
    return WrapError<TPMError>(std::move(err),
                               "Error generating random secret");
  }
  DCHECK_EQ(secret_value->size(), kSecretSizeBytes);
  // Seal the secret value.
  std::string sealed_value;
  if (auto err = CreateError<TPM2Error>(trunks->tpm_utility->SealData(
          secret_value->to_string(), policy_digest, "",
          /*require_admin_with_policy=*/true, session->GetDelegate(),
          &sealed_value))) {
    return WrapError<TPMError>(std::move(err), "Error sealing secret data");
  }
  // Fill the resulting proto with data required for unsealing.
  structure::Tpm2PolicySignedData data;
  data.public_key_spki_der = public_key_spki_der;
  data.srk_wrapped_secret = BlobFromString(sealed_value);
  data.scheme = scheme;
  data.hash_alg = hash_alg;
  data.default_pcr_policy_digest = BlobFromString(default_pcr_policy_digest);
  data.extended_pcr_policy_digest = BlobFromString(extended_pcr_policy_digest);
  *sealed_secret_data = data;
  return nullptr;
}

StatusChain<TPMErrorBase>
SignatureSealingBackendTpm2Impl::CreateUnsealingSession(
    const structure::SignatureSealedData& sealed_secret_data,
    const Blob& public_key_spki_der,
    const std::vector<structure::ChallengeSignatureAlgorithm>& key_algorithms,
    const std::set<uint32_t>& pcr_set,
    const Blob& /* delegate_blob */,
    const Blob& /* delegate_secret */,
    bool /* locked_to_single_user */,
    std::unique_ptr<SignatureSealingBackend::UnsealingSession>*
        unsealing_session) {
  // Validate the parameters.
  auto* sealed_secret_data_ptr =
      std::get_if<structure::Tpm2PolicySignedData>(&sealed_secret_data);
  if (!sealed_secret_data_ptr) {
    return CreateError<TPMError>(
        "Sealed data is empty or uses unexpected method",
        TPMRetryAction::kNoRetry);
  }
  const structure::Tpm2PolicySignedData& data = *sealed_secret_data_ptr;

  if (data.public_key_spki_der.empty()) {
    return CreateError<TPMError>("Empty public key", TPMRetryAction::kNoRetry);
  }
  if (data.srk_wrapped_secret.empty()) {
    return CreateError<TPMError>("Empty SRK wrapped secret",
                                 TPMRetryAction::kNoRetry);
  }
  if (!data.scheme.has_value()) {
    return CreateError<TPMError>("Empty scheme", TPMRetryAction::kNoRetry);
  }
  if (!data.hash_alg.has_value()) {
    return CreateError<TPMError>("Empty hash algorithm",
                                 TPMRetryAction::kNoRetry);
  }

  if (data.public_key_spki_der != public_key_spki_der) {
    return CreateError<TPMError>("Wrong subject public key info",
                                 TPMRetryAction::kNoRetry);
  }
  if (!base::IsValueInRangeForNumericType<TPM_ALG_ID>(data.scheme.value())) {
    return CreateError<TPMError>("Error parsing signature scheme",
                                 TPMRetryAction::kNoRetry);
  }
  const TPM_ALG_ID scheme = static_cast<TPM_ALG_ID>(data.scheme.value());
  if (!base::IsValueInRangeForNumericType<TPM_ALG_ID>(data.hash_alg.value())) {
    return CreateError<TPMError>("Error parsing signature hash algorithm",
                                 TPMRetryAction::kNoRetry);
  }
  const TPM_ALG_ID hash_alg = static_cast<TPM_ALG_ID>(data.hash_alg.value());
  std::optional<structure::ChallengeSignatureAlgorithm> chosen_algorithm;
  for (auto algorithm : key_algorithms) {
    TPM_ALG_ID current_scheme = TPM_ALG_NULL;
    TPM_ALG_ID current_hash_alg = TPM_ALG_NULL;
    if (GetAlgIdsByAlgorithm(algorithm, &current_scheme, &current_hash_alg) &&
        current_scheme == scheme && current_hash_alg == hash_alg) {
      chosen_algorithm = algorithm;
      break;
    }
  }
  if (!chosen_algorithm) {
    return CreateError<TPMError>("Key doesn't support required algorithm",
                                 TPMRetryAction::kNoRetry);
  }
  // Obtain the trunks context to be used for the whole unsealing session.
  Tpm2Impl::TrunksClientContext* trunks = nullptr;
  if (!tpm_->GetTrunksContext(&trunks)) {
    return CreateError<TPMError>("Failed to get trunks context",
                                 TPMRetryAction::kNoRetry);
  }
  // Start a policy session that will be used for obtaining the TPM nonce and
  // unsealing the secret value.
  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetPolicySession();
  if (auto err = CreateError<TPM2Error>(
          policy_session->StartUnboundSession(true, false))) {
    return WrapError<TPMError>(std::move(err),
                               "Error starting a policy session");
  }

  if (data.default_pcr_policy_digest.empty() ||
      data.extended_pcr_policy_digest.empty()) {
    return CreateError<TPMError>("Empty PCR policy digests",
                                 TPMRetryAction::kNoRetry);
  }

  if (data.default_pcr_policy_digest.size() != SHA256_DIGEST_SIZE ||
      data.extended_pcr_policy_digest.size() != SHA256_DIGEST_SIZE) {
    return CreateError<TPMError>("Invalid policy digest size",
                                 TPMRetryAction::kNoRetry);
  }

  // The PCR map with empty user name.
  std::map<uint32_t, std::string> pcr_map;

  // Create a PCR map with empty strings to use current PCR values.
  for (uint32_t pcr_index : pcr_set) {
    pcr_map[pcr_index] = std::string();
  }

  if (auto err = CreateError<TPM2Error>(policy_session->PolicyPCR(pcr_map))) {
    return WrapError<TPMError>(std::move(err),
                               "Error restricting policy to PCRs");
  }

  // Update the policy with the disjunction of their policy digests.
  // Note: The order of items in this vector is important, it must match the
  // order used in the CreateSealedSecret() function, and should never change
  // due to backwards compatibility.
  std::vector<std::string> pcr_policy_digests;
  pcr_policy_digests.push_back(BlobToString(data.default_pcr_policy_digest));
  pcr_policy_digests.push_back(BlobToString(data.extended_pcr_policy_digest));

  if (auto err = CreateError<TPM2Error>(
          policy_session->PolicyOR(pcr_policy_digests))) {
    return WrapError<TPMError>(
        std::move(err),
        "Error restricting policy to logical disjunction of PCRs");
  }

  // Obtain the TPM nonce.
  std::string tpm_nonce;
  if (!policy_session->GetDelegate()->GetTpmNonce(&tpm_nonce)) {
    return CreateError<TPMError>("Error obtaining TPM nonce",
                                 TPMRetryAction::kNoRetry);
  }

  // Create the unsealing session that will keep the required state.
  *unsealing_session = std::make_unique<UnsealingSessionTpm2Impl>(
      tpm_, trunks, data.srk_wrapped_secret, public_key_spki_der,
      *chosen_algorithm, scheme, hash_alg, std::move(policy_session),
      BlobFromString(tpm_nonce));
  return nullptr;
}

}  // namespace cryptohome
