// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <sys/utsname.h>

#include "absl/status/status.h"
#include "attestation/proto_bindings/interface.pb.h"
#include "attestation-client/attestation/dbus-proxies.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "tpm_manager/proto_bindings/tpm_manager.pb.h"
#include "vboot/crossystem.h"

namespace {

constexpr int kWaitForServicesTimeoutMs = 2000;

// Converts a brillo::Error* to string for printing.
std::string BrilloErrorToString(brillo::Error* err) {
  std::string result;
  if (err) {
    result = base::StrCat({"(", err->GetDomain(), ", ", err->GetCode(), ", ",
                           err->GetMessage(), ")"});
  } else {
    result = "(null)";
  }
  return result;
}

std::string TpmPropertyToStr(uint32_t value) {
  std::string str;

  for (int i = 0, shift = 24; i < 4; i++, shift -= 8) {
    auto c = static_cast<char>((value >> shift) & 0xFF);
    if (c == 0)
      break;
    str.push_back((c >= 32 && c < 127) ? c : ' ');
  }

  return str;
}
}  // namespace

namespace secagentd {

AgentPlugin::AgentPlugin(
    scoped_refptr<MessageSenderInterface> message_sender,
    std::unique_ptr<org::chromium::AttestationProxy> attestation_proxy,
    std::unique_ptr<org::chromium::TpmManagerProxy> tpm_manager_proxy,
    base::OnceCallback<void()> cb)
    : weak_ptr_factory_(this), message_sender_(message_sender) {
  CHECK(message_sender != nullptr);
  attestation_proxy_ = std::move(attestation_proxy);
  tpm_manager_proxy_ = std::move(tpm_manager_proxy);
  daemon_cb_ = std::move(cb);
}

std::string AgentPlugin::GetName() const {
  return "AgentPlugin";
}

absl::Status AgentPlugin::Activate() {
  StartInitializingAgentProto();

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&AgentPlugin::SendAgentStartEvent,
                     weak_ptr_factory_.GetWeakPtr()),
      // Add delay for tpm_manager and attestation to initialize.
      base::Seconds(1));

  return absl::OkStatus();
}

void AgentPlugin::StartInitializingAgentProto() {
  attestation_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(&AgentPlugin::GetBootInformation,
                     weak_ptr_factory_.GetWeakPtr()));
  tpm_manager_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(&AgentPlugin::GetTpmInformation,
                     weak_ptr_factory_.GetWeakPtr()));

  char buffer[VB_MAX_STRING_PROPERTY];
  auto get_fwid_rv =
      VbGetSystemPropertyString("fwid", buffer, std::size(buffer));

  // Get linux version.
  struct utsname buf;
  int get_uname_rv = uname(&buf);

  base::AutoLock lock(tcb_attributes_lock_);
  if (get_fwid_rv) {
    tcb_attributes_.set_system_firmware_version(get_fwid_rv);
  } else {
    LOG(ERROR) << "Failed to retrieve fwid";
  }
  if (!get_uname_rv) {
    tcb_attributes_.set_linux_kernel_version(buf.release);
  } else {
    LOG(ERROR) << "Failed to retrieve uname";
  }
}

void AgentPlugin::GetBootInformation(bool available) {
  if (!available) {
    LOG(ERROR) << "Failed waiting for attestation to become available";
    return;
  }

  // Get boot information.
  attestation::GetStatusRequest request;
  attestation::GetStatusReply out_reply;
  brillo::ErrorPtr error;

  if (!attestation_proxy_->GetStatus(request, &out_reply, &error,
                                     kWaitForServicesTimeoutMs) ||
      error.get()) {
    LOG(ERROR) << "Failed to get boot information "
               << BrilloErrorToString(error.get());
    return;
  }
  // TODO(b/241578769): Handle kCrosFlexUefiSecureBoot.
  base::AutoLock lock(tcb_attributes_lock_);
  tcb_attributes_.set_firmware_secure_boot(
      out_reply.verified_boot()
          ? cros_xdr::reporting::
                TcbAttributes_FirmwareSecureBoot_CROS_VERIFIED_BOOT
          : cros_xdr::reporting::TcbAttributes_FirmwareSecureBoot_NONE);
}

void AgentPlugin::GetTpmInformation(bool available) {
  if (!available) {
    LOG(ERROR) << "Failed waiting for tpm_manager to become available";
    return;
  }

  // Get TPM information.
  tpm_manager::GetVersionInfoRequest request;
  tpm_manager::GetVersionInfoReply out_reply;
  brillo::ErrorPtr error;

  if (!tpm_manager_proxy_->GetVersionInfo(request, &out_reply, &error,
                                          kWaitForServicesTimeoutMs) ||
      error.get()) {
    LOG(ERROR) << "Failed to get TPM information "
               << BrilloErrorToString(error.get());
    return;
  }
  base::AutoLock lock(tcb_attributes_lock_);
  auto security_chip = tcb_attributes_.mutable_security_chip();
  if (out_reply.has_gsc_version()) {
    switch (out_reply.gsc_version()) {
      case tpm_manager::GSC_VERSION_NOT_GSC: {
        security_chip->set_kind(
            cros_xdr::reporting::TcbAttributes_SecurityChip::Kind::
                TcbAttributes_SecurityChip_Kind_TPM);
        break;
      }
      case tpm_manager::GSC_VERSION_CR50:
      case tpm_manager::GSC_VERSION_TI50:
        security_chip->set_kind(
            cros_xdr::reporting::TcbAttributes_SecurityChip::Kind::
                TcbAttributes_SecurityChip_Kind_GOOGLE_SECURITY_CHIP);
    }
    auto family = TpmPropertyToStr(out_reply.family());
    auto level = std::to_string((out_reply.spec_level() >> 32) & 0xffffffff);
    security_chip->set_chip_version(base::StringPrintf(
        "%s.%s.%s", family.c_str(), level.c_str(),
        std::to_string(out_reply.spec_level() & 0xffffffff).c_str()));
    security_chip->set_spec_family(family);
    security_chip->set_spec_level(level);
    security_chip->set_manufacturer(TpmPropertyToStr(out_reply.manufacturer()));
    security_chip->set_vendor_id(out_reply.vendor_specific());
    security_chip->set_tpm_model(std::to_string(out_reply.tpm_model()));
    security_chip->set_firmware_version(
        std::to_string(out_reply.firmware_version()));
  }
}

void AgentPlugin::SendAgentStartEvent() {
  auto agent_event = std::make_unique<cros_xdr::reporting::XdrAgentEvent>();
  base::AutoLock lock(tcb_attributes_lock_);
  agent_event->mutable_agent_start()->mutable_tcb()->CopyFrom(tcb_attributes_);
  message_sender_->SendMessage(
      reporting::CROS_SECURITY_AGENT, agent_event->mutable_common(),
      std::move(agent_event),
      base::BindOnce(&AgentPlugin::StartEventStatusCallback,
                     weak_ptr_factory_.GetWeakPtr()));
}

void AgentPlugin::SendAgentHeartbeatEvent() {
  // Create agent heartbeat event.
  auto agent_event = std::make_unique<cros_xdr::reporting::XdrAgentEvent>();
  base::AutoLock lock(tcb_attributes_lock_);
  agent_event->mutable_agent_heartbeat()->mutable_tcb()->CopyFrom(
      tcb_attributes_);
  message_sender_->SendMessage(reporting::CROS_SECURITY_AGENT,
                               agent_event->mutable_common(),
                               std::move(agent_event), std::nullopt);
}

void AgentPlugin::StartEventStatusCallback(reporting::Status status) {
  if (status.ok()) {
    // Start heartbeat timer.
    agent_heartbeat_timer_.Start(
        FROM_HERE, base::Minutes(5),
        base::BindRepeating(&AgentPlugin::SendAgentHeartbeatEvent,
                            weak_ptr_factory_.GetWeakPtr()));

    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(daemon_cb_));
  } else {
    LOG(ERROR) << "Agent Start failed to send. Will retry in 3s.";
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AgentPlugin::SendAgentStartEvent,
                       weak_ptr_factory_.GetWeakPtr()),
        base::Seconds(3));
  }
}

}  // namespace secagentd
