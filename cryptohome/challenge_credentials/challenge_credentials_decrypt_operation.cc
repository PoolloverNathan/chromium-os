// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_decrypt_operation.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <libhwsec/status.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/structures.h"

using brillo::Blob;
using brillo::SecureBlob;
using cryptohome::error::CryptohomeError;
using cryptohome::error::CryptohomeTPMError;
using cryptohome::error::ErrorAction;
using cryptohome::error::ErrorActionSet;
using cryptohome::error::NoErrorAction;
using hwsec::TPMError;
using hwsec::TPMErrorBase;
using hwsec::TPMRetryAction;
using hwsec_foundation::status::MakeStatus;
using hwsec_foundation::status::OkStatus;
using hwsec_foundation::status::StatusChain;
using HwsecAlgorithm = hwsec::CryptohomeFrontend::SignatureSealingAlgorithm;

namespace cryptohome {

namespace {

HwsecAlgorithm ConvertToHwsecAlgorithm(
    structure::ChallengeSignatureAlgorithm algorithm) {
  switch (algorithm) {
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1:
      return HwsecAlgorithm::kRsassaPkcs1V15Sha1;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256:
      return HwsecAlgorithm::kRsassaPkcs1V15Sha256;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384:
      return HwsecAlgorithm::kRsassaPkcs1V15Sha384;
    case structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512:
      return HwsecAlgorithm::kRsassaPkcs1V15Sha512;
  }
  NOTREACHED() << "Unknown algorithm, fallback to SHA1.";
  return HwsecAlgorithm::kRsassaPkcs1V15Sha1;
}

structure::ChallengeSignatureAlgorithm ConvertFromHwsecAlgorithm(
    HwsecAlgorithm algorithm) {
  switch (algorithm) {
    case HwsecAlgorithm::kRsassaPkcs1V15Sha1:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1;
    case HwsecAlgorithm::kRsassaPkcs1V15Sha256:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256;
    case HwsecAlgorithm::kRsassaPkcs1V15Sha384:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha384;
    case HwsecAlgorithm::kRsassaPkcs1V15Sha512:
      return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha512;
  }
  NOTREACHED() << "Unknown algorithm, fallback to SHA1.";
  return structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha1;
}

}  // namespace

ChallengeCredentialsDecryptOperation::ChallengeCredentialsDecryptOperation(
    KeyChallengeService* key_challenge_service,
    hwsec::CryptohomeFrontend* hwsec,
    const std::string& account_id,
    const structure::ChallengePublicKeyInfo& public_key_info,
    const structure::SignatureChallengeInfo& keyset_challenge_info,
    CompletionCallback completion_callback)
    : ChallengeCredentialsOperation(key_challenge_service),
      account_id_(account_id),
      public_key_info_(public_key_info),
      keyset_challenge_info_(keyset_challenge_info),
      completion_callback_(std::move(completion_callback)),
      hwsec_(hwsec) {}

ChallengeCredentialsDecryptOperation::~ChallengeCredentialsDecryptOperation() =
    default;

void ChallengeCredentialsDecryptOperation::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  StatusChain<CryptohomeTPMError> status = StartProcessing();
  if (!status.ok()) {
    Resolve(MakeStatus<CryptohomeTPMError>(
                CRYPTOHOME_ERR_LOC(kLocChalCredDecryptCantStartProcessing),
                NoErrorAction())
                .Wrap(std::move(status)));
    // |this| can be already destroyed at this point.
  }
}

void ChallengeCredentialsDecryptOperation::Abort(TPMStatus status) {
  DCHECK(thread_checker_.CalledOnValidThread());
  Resolve(MakeStatus<CryptohomeTPMError>(
              CRYPTOHOME_ERR_LOC(kLocChalCredDecryptOperationAborted))
              .Wrap(std::move(status)));
  // |this| can be already destroyed at this point.
}

StatusChain<CryptohomeTPMError>
ChallengeCredentialsDecryptOperation::StartProcessing() {
  if (!hwsec_) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoHwsecBackend),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  if (!public_key_info_.signature_algorithm.size()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoPubKeySigSize),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }

  if (public_key_info_.public_key_spki_der !=
      keyset_challenge_info_.public_key_spki_der) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSPKIPubKeyMismatch),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }

  StatusChain<CryptohomeTPMError> status = StartProcessingSalt();
  if (!status.ok()) {
    return MakeStatus<CryptohomeTPMError>(
               CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSaltProcessingFailed),
               NoErrorAction())
        .Wrap(std::move(status));
  }
  // TODO(crbug.com/842791): This is buggy: |this| may be already deleted by
  // that point, in case when the salt's challenge request failed synchronously.
  return StartProcessingSealedSecret();
}

StatusChain<CryptohomeTPMError>
ChallengeCredentialsDecryptOperation::StartProcessingSalt() {
  if (keyset_challenge_info_.salt.empty()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  if (public_key_info_.public_key_spki_der.empty()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(
            kLocChalCredDecryptNoSPKIPubKeyDERWhileProcessingSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  if (!keyset_challenge_info_.salt_signature_algorithm.has_value()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptNoSaltSigAlgoWhileProcessingSalt),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  const Blob& salt = keyset_challenge_info_.salt;
  // IMPORTANT: Verify that the salt is correctly prefixed. See the comment on
  // GetChallengeCredentialsSaltConstantPrefix() for details. Note also that, as
  // an extra validation, we require the salt to contain at least one extra byte
  // after the prefix.
  const Blob& salt_constant_prefix =
      GetChallengeCredentialsSaltConstantPrefix();
  if (salt.size() <= salt_constant_prefix.size() ||
      !std::equal(salt_constant_prefix.begin(), salt_constant_prefix.end(),
                  salt.begin())) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSaltPrefixIncorrect),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }
  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der, salt,
      keyset_challenge_info_.salt_signature_algorithm.value(),
      base::BindOnce(
          &ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return OkStatus<CryptohomeTPMError>();
}

StatusChain<CryptohomeTPMError>
ChallengeCredentialsDecryptOperation::StartProcessingSealedSecret() {
  if (public_key_info_.public_key_spki_der.empty()) {
    return MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(
            kLocChalCredDecryptNoSPKIPubKeyDERWhileProcessingSecret),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry);
  }

  std::vector<HwsecAlgorithm> key_sealing_algorithms;
  for (auto algo : public_key_info_.signature_algorithm) {
    key_sealing_algorithms.push_back(ConvertToHwsecAlgorithm(algo));
  }

  hwsec::StatusOr<hwsec::CryptohomeFrontend::ChallengeResult> challenge =
      hwsec_->ChallengeWithSignatureAndCurrentUser(
          keyset_challenge_info_.sealed_secret,
          public_key_info_.public_key_spki_der, key_sealing_algorithms);
  if (!challenge.ok()) {
    TPMStatus status =
        MakeStatus<CryptohomeTPMError>(std::move(challenge).status());
    return MakeStatus<CryptohomeTPMError>(
               CRYPTOHOME_ERR_LOC(
                   kLocChalCredDecryptCreateUnsealingSessionFailed),
               NoErrorAction())
        .Wrap(std::move(status));
  }

  challenge_id_ = challenge.value().challenge_id;

  MakeKeySignatureChallenge(
      account_id_, public_key_info_.public_key_spki_der,
      challenge.value().challenge,
      ConvertFromHwsecAlgorithm(challenge.value().algorithm),
      base::BindOnce(
          &ChallengeCredentialsDecryptOperation::OnUnsealingChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return OkStatus<CryptohomeTPMError>();
}

void ChallengeCredentialsDecryptOperation::OnSaltChallengeResponse(
    TPMStatusOr<std::unique_ptr<brillo::Blob>> salt_signature) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!salt_signature.ok()) {
    Resolve(MakeStatus<CryptohomeTPMError>(
                CRYPTOHOME_ERR_LOC(kLocChalCredDecryptSaltResponseNoSignature))
                .Wrap(std::move(salt_signature).status()));
    // |this| can be already destroyed at this point.
    return;
  }
  salt_signature_ = std::move(salt_signature).value();
  ProceedIfChallengesDone();
}

void ChallengeCredentialsDecryptOperation::OnUnsealingChallengeResponse(
    TPMStatusOr<std::unique_ptr<brillo::Blob>> challenge_signature_status) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!challenge_signature_status.ok()) {
    Resolve(
        MakeStatus<CryptohomeTPMError>(
            CRYPTOHOME_ERR_LOC(kLocChalCredDecryptUnsealingResponseNoSignature))
            .Wrap(std::move(challenge_signature_status).status()));
    // |this| can be already destroyed at this point.
    return;
  }

  std::unique_ptr<brillo::Blob> challenge_signature =
      std::move(challenge_signature_status).value();

  if (!challenge_id_.has_value()) {
    Resolve(MakeStatus<CryptohomeTPMError>(
        CRYPTOHOME_ERR_LOC(kLocChalCredDecryptUnsealingResponseNoChallenge),
        ErrorActionSet({ErrorAction::kDevCheckUnexpectedState}),
        TPMRetryAction::kNoRetry));
    return;
  }

  hwsec::StatusOr<brillo::SecureBlob> unsealed_secret =
      hwsec_->UnsealWithChallenge(challenge_id_.value(), *challenge_signature);

  if (!unsealed_secret.ok()) {
    TPMStatus status =
        MakeStatus<CryptohomeTPMError>(std::move(unsealed_secret).status());
    Resolve(MakeStatus<CryptohomeTPMError>(
                CRYPTOHOME_ERR_LOC(kLocChalCredDecryptUnsealFailed),
                NoErrorAction())
                .Wrap(std::move(status)));
    // |this| can be already destroyed at this point.
    return;
  }
  unsealed_secret_ = std::make_unique<SecureBlob>(unsealed_secret.value());
  ProceedIfChallengesDone();
}

void ChallengeCredentialsDecryptOperation::ProceedIfChallengesDone() {
  if (!salt_signature_ || !unsealed_secret_)
    return;
  auto passkey = std::make_unique<brillo::SecureBlob>(
      ConstructPasskey(*unsealed_secret_, *salt_signature_));
  Resolve(ChallengeCredentialsHelper::GenerateNewOrDecryptResult(
      /* signature_challenge_info */ nullptr, std::move(passkey)));
  // |this| can be already destroyed at this point.
}

void ChallengeCredentialsDecryptOperation::Resolve(
    TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
        result) {
  // Invalidate weak pointers in order to cancel all jobs that are currently
  // waiting, to prevent them from running and consuming resources after our
  // abortion (in case |this| doesn't get destroyed immediately).
  //
  // Note that the already issued challenge requests don't get cancelled, so
  // their responses will be just ignored should they arrive later. The request
  // cancellation is not supported by the challenges IPC API currently, neither
  // it is supported by the API for smart card drivers in Chrome OS.
  weak_ptr_factory_.InvalidateWeakPtrs();
  Complete(&completion_callback_, std::move(result));
}

}  // namespace cryptohome
