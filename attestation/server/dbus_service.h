//
// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ATTESTATION_SERVER_DBUS_SERVICE_H_
#define ATTESTATION_SERVER_DBUS_SERVICE_H_

#include <memory>

#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "attestation/common/attestation_interface.h"

namespace attestation {

using CompletionAction =
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction;

// Handles D-Bus calls to the attestation daemon.
class DBusService {
 public:
  // DBusService does not take ownership of |service|; it must remain valid for
  // the lifetime of the DBusService instance.
  DBusService(const scoped_refptr<dbus::Bus>& bus,
              AttestationInterface* service);
  virtual ~DBusService() = default;

  // Connects to D-Bus system bus and exports methods.
  void Register(const CompletionAction& callback);

  // Useful for testing.
  void set_service(AttestationInterface* service) { service_ = service; }

 private:
  friend class DBusServiceTest;

  // Handles a CreateGoogleAttestedKey D-Bus call.
  void HandleCreateGoogleAttestedKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const CreateGoogleAttestedKeyReply&>> response,
      const CreateGoogleAttestedKeyRequest& request);

  // Handles a GetKeyInfo D-Bus call.
  void HandleGetKeyInfo(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                            const GetKeyInfoReply&>> response,
                        const GetKeyInfoRequest& request);

  // Handles a GetEndorsementInfo D-Bus call.
  void HandleGetEndorsementInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const GetEndorsementInfoReply&>> response,
      const GetEndorsementInfoRequest& request);

  // Handles a GetAttestationKeyInfo D-Bus call.
  void HandleGetAttestationKeyInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const GetAttestationKeyInfoReply&>> response,
      const GetAttestationKeyInfoRequest& request);

  // Handles a ActivateAttestationKey D-Bus call.
  void HandleActivateAttestationKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const ActivateAttestationKeyReply&>> response,
      const ActivateAttestationKeyRequest& request);

  // Handles a CreateCertifiableKey D-Bus call.
  void HandleCreateCertifiableKey(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const CreateCertifiableKeyReply&>> response,
      const CreateCertifiableKeyRequest& request);

  // Handles a Decrypt D-Bus call.
  void HandleDecrypt(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<const DecryptReply&>> response,
      const DecryptRequest& request);

  // Handles a Sign D-Bus call.
  void HandleSign(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<const SignReply&>>
          response,
      const SignRequest& request);

  // Handles a RegisterKeyWithChapsToken D-Bus call.
  void HandleRegisterKeyWithChapsToken(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const RegisterKeyWithChapsTokenReply&>> response,
      const RegisterKeyWithChapsTokenRequest& request);

  // Handles a GetStatus D-Bus call.
  void HandleGetStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const GetStatusReply&>> response,
      const GetStatusRequest& request);

  // Handles a Verify D-Bus call.
  void HandleVerify(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const VerifyReply&>> response,
      const VerifyRequest& request);

  // Handles a CreateEnrollRequest D-Bus call.
  void HandleCreateEnrollRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const CreateEnrollRequestReply&>> response,
      const CreateEnrollRequestRequest& request);

  // Handles a FinishEnroll D-Bus call.
  void HandleFinishEnroll(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const FinishEnrollReply&>> response,
      const FinishEnrollRequest& request);

  // Handles a CreateCertificateRequest D-Bus call.
  void HandleCreateCertificateRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const CreateCertificateRequestReply&>> response,
      const CreateCertificateRequestRequest& request);

  // Handles a FinishCertificateRequest D-Bus call.
  void HandleFinishCertificateRequest(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const FinishCertificateRequestReply&>> response,
      const FinishCertificateRequestRequest& request);

  // Handles a SignEnterpriseChallenge D-Bus call.
  void HandleSignEnterpriseChallenge(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const SignEnterpriseChallengeReply&>> response,
      const SignEnterpriseChallengeRequest& request);

  // Handles a SignSimpleChallenge D-Bus call.
  void HandleSignSimpleChallenge(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const SignSimpleChallengeReply&>> response,
      const SignSimpleChallengeRequest& request);

  // Handles a SetKeyPayload D-Bus call.
  void HandleSetKeyPayload(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const SetKeyPayloadReply&>> response,
      const SetKeyPayloadRequest& request);

  // Handles a DeleteKeys D-Bus call.
  void HandleDeleteKeys(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const DeleteKeysReply&>> response,
      const DeleteKeysRequest& request);

  // Handles a ResetIdentity D-Bus call.
  void HandleResetIdentity(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const ResetIdentityReply&>> response,
      const ResetIdentityRequest& request);

  brillo::dbus_utils::DBusObject dbus_object_;
  AttestationInterface* service_;

  DISALLOW_COPY_AND_ASSIGN(DBusService);
};

}  // namespace attestation

#endif  // ATTESTATION_SERVER_DBUS_SERVICE_H_
