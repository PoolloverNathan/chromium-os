// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_manager.h"

#include <cinttypes>
#include <memory>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

#include "dlcservice/dlc.h"
#if USE_LVM_STATEFUL_PARTITION
#include "dlcservice/lvm/dlc_lvm.h"
#endif  // USE_LVM_STATEFUL_PARTITION
#include "dlcservice/error.h"
#include "dlcservice/system_state.h"
#include "dlcservice/types.h"
#include "dlcservice/utils.h"

using brillo::ErrorPtr;
using brillo::MessageLoop;

namespace dlcservice {

namespace {
DlcIdList ToDlcIdList(const DlcMap& dlcs,
                      const std::function<bool(const DlcType&)>& filter) {
  DlcIdList list;
  for (const auto& pair : dlcs) {
    if (filter(pair.second))
      list.push_back(pair.first);
  }
  return list;
}
}  // namespace

DlcManager::~DlcManager() = default;

void DlcManager::Initialize() {
  supported_.clear();

  // Initialize supported DLC(s).
  for (const auto& id : ScanDirectory(SystemState::Get()->manifest_dir())) {
    auto result =
#if USE_LVM_STATEFUL_PARTITION
        supported_.emplace(id, std::make_unique<DlcLvm>(id));
#else
        supported_.emplace(id, std::make_unique<DlcBase>(id));
#endif  // USE_LVM_STATEFUL_PARTITION
    if (!result.first->second->Initialize()) {
      LOG(ERROR) << "Failed to initialize DLC " << id;
      supported_.erase(id);
    }
  }

  CleanupUnsupportedDlcs();
}

void DlcManager::CleanupUnsupportedDlcs() {
  auto* system_state = SystemState::Get();
  // Delete deprecated DLC(s) in content directory.
  for (const auto& id : ScanDirectory(system_state->content_dir())) {
    brillo::ErrorPtr tmp_err;
    if (GetDlc(id, &tmp_err) != nullptr)
      continue;
    for (const auto& path : DlcBase::GetPathsToDelete(id))
      if (base::PathExists(path)) {
        if (!base::DeletePathRecursively(path))
          PLOG(ERROR) << "Failed to delete path=" << path;
        else
          LOG(INFO) << "Deleted path=" << path << " for deprecated DLC=" << id;
      }
  }

  // Delete the unsupported/preload not allowed DLC(s) in the preloaded
  // directory.
  auto preloaded_content_dir = system_state->preloaded_content_dir();
  for (const auto& id : ScanDirectory(preloaded_content_dir)) {
    brillo::ErrorPtr tmp_err;
    auto* dlc = GetDlc(id, &tmp_err);
    if (dlc != nullptr && dlc->IsPreloadAllowed())
      continue;

    // Preloading is not allowed for this image so it will be deleted.
    auto path = JoinPaths(preloaded_content_dir, id);
    if (!base::DeletePathRecursively(path))
      PLOG(ERROR) << "Failed to delete path=" << path;
    else
      LOG(INFO) << "Deleted path=" << path
                << " for unsupported/preload not allowed DLC=" << id;
  }
}

DlcBase* DlcManager::GetDlc(const DlcId& id, brillo::ErrorPtr* err) {
  const auto& iter = supported_.find(id);
  if (iter == supported_.end()) {
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Passed unsupported DLC=%s", id.c_str()));
    return nullptr;
  }
  return iter->second.get();
}

DlcIdList DlcManager::GetInstalled() {
  // TODO(kimjae): Once update_engine repeatedly calls into |GetInstalled()| for
  // updating update, need to handle clearing differently.
  return ToDlcIdList(supported_,
                     [](const DlcType& dlc) { return dlc->IsInstalled(); });
}

DlcIdList DlcManager::GetExistingDlcs() {
  return ToDlcIdList(supported_,
                     [](const DlcType& dlc) { return dlc->HasContent(); });
}

DlcIdList DlcManager::GetDlcsToUpdate() {
  return ToDlcIdList(
      supported_, [](const DlcType& dlc) { return dlc->MakeReadyForUpdate(); });
}

DlcIdList DlcManager::GetSupported() {
  return ToDlcIdList(supported_, [](const DlcType&) { return true; });
}

bool DlcManager::InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) {
  DCHECK(err);
  bool ret = true;
  for (const auto& id : ids) {
    auto* dlc = GetDlc(id, err);
    if (dlc == nullptr) {
      LOG(WARNING) << "Trying to complete installation for unsupported DLC="
                   << id;
      ret = false;
    } else if (!dlc->InstallCompleted(err)) {
      PLOG(WARNING) << "Failed to complete install.";
      ret = false;
    }
  }
  // The returned error pertains to the last error happened. We probably don't
  // need any accumulation of errors.
  return ret;
}

bool DlcManager::UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) {
  DCHECK(err);
  bool ret = true;
  for (const auto& id : ids) {
    auto* dlc = GetDlc(id, err);
    if (dlc == nullptr) {
      LOG(WARNING) << "Trying to complete update for unsupported DLC=" << id;
      ret = false;
    } else if (!dlc->UpdateCompleted(err)) {
      LOG(WARNING) << "Failed to complete update.";
      ret = false;
    }
  }
  // The returned error pertains to the last error happened. We probably don't
  // need any accumulation of errors.
  return ret;
}

bool DlcManager::Install(const InstallRequest& install_request,
                         bool* external_install_needed,
                         ErrorPtr* err) {
  DCHECK(err);
  const auto id = install_request.id();
  auto* dlc = GetDlc(id, err);
  if (dlc == nullptr) {
    return false;
  }

  dlc->SetReserve(install_request.reserve());

  // If the DLC is being installed, nothing can be done anymore.
  if (dlc->IsInstalling()) {
    return true;
  }

  // Otherwise proceed to install the DLC.
  if (!dlc->Install(err)) {
    Error::AddInternalTo(
        err, FROM_HERE, error::kFailedInternal,
        base::StringPrintf("Failed to initialize installation for DLC=%s",
                           id.c_str()));
    return false;
  }

  // If the DLC is now in installing state, it means it now needs update_engine
  // installation.
  *external_install_needed = dlc->IsInstalling();
  return true;
}

bool DlcManager::Uninstall(const DlcId& id, ErrorPtr* err) {
  DCHECK(err);
  auto* dlc = GetDlc(id, err);
  if (dlc == nullptr) {
    return false;
  }
  return dlc->Uninstall(err);
}

bool DlcManager::FinishInstall(const DlcId& id, ErrorPtr* err) {
  DCHECK(err);
  auto dlc = GetDlc(id, err);
  if (!dlc) {
    *err = Error::Create(FROM_HERE, kErrorInvalidDlc,
                         "Finishing installation for invalid DLC.");
    return false;
  }
  if (!dlc->IsInstalling()) {
    *err = Error::Create(
        FROM_HERE, kErrorInternal,
        "Finishing installation for a DLC that is not being installed.");
    return false;
  }
  return dlc->FinishInstall(/*installed_by_ue=*/true, err);
}

bool DlcManager::CancelInstall(const DlcId& id,
                               const ErrorPtr& err_in,
                               ErrorPtr* err) {
  DCHECK(err);
  auto* dlc = GetDlc(id, err);
  if (dlc == nullptr) {
    *err = Error::Create(FROM_HERE, kErrorInvalidDlc,
                         "Cancelling installation for invalid DLC.");
    return false;
  }
  return !dlc->IsInstalling() || dlc->CancelInstall(err_in, err);
}

void DlcManager::ChangeProgress(double progress) {
  for (auto& pr : supported_) {
    auto& dlc = pr.second;
    if (dlc->IsInstalling()) {
      dlc->ChangeProgress(progress);
    }
  }
}

}  // namespace dlcservice
