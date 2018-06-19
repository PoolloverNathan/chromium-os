// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <memory>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>

#include "smbprovider/constants.h"
#include "smbprovider/in_memory_credential_store.h"
#include "smbprovider/kerberos_artifact_client.h"
#include "smbprovider/kerberos_artifact_synchronizer.h"
#include "smbprovider/mount_manager.h"
#include "smbprovider/samba_interface_impl.h"
#include "smbprovider/smbprovider.h"

namespace smbprovider {

namespace {

// Helper method to set $HOME variable to a temporary path that only
// smbproviderd user can access.
bool SetHomeEnvironmentVariable() {
  if (setenv(kHomeEnvironmentVariable, kSmbProviderHome, 1 /* overwrite */) !=
      0) {
    PLOG(ERROR) << "Failed to set $HOME variable";
    return false;
  }
  return true;
}

std::string GetKrb5ConfLocation() {
  return std::string(kSmbProviderHome) + kKrb5ConfLocation;
}

std::string GetCCacheLocation() {
  return std::string(kSmbProviderHome) + kCCacheLocation;
}

std::string GetKrb5TraceLocation() {
  return std::string(kSmbProviderHome) + kKrbTraceLocation;
}

std::string GetKrb5ConfPath() {
  return GetKrb5ConfLocation() + kKrb5ConfFile;
}

std::string GetCCachePath() {
  return GetCCacheLocation() + kCCacheFile;
}

std::string GetKrb5TracePath() {
  return GetKrb5TraceLocation() + kKrbTraceFile;
}

bool SetKrb5ConfigEnviornmentVariable() {
  if (setenv(kKrb5ConfigEnvironmentVariable, GetKrb5ConfPath().c_str(),
             1 /* overwrite */) != 0) {
    PLOG(ERROR) << "Failed to set $KRB5_CONFIG variable";
    return false;
  }
  return true;
}

bool SetKrb5CCNameEnvironmentVariable() {
  if (setenv(kKrb5CCNameEnvironmentVariable, GetCCachePath().c_str(),
             1 /* overwrite */) != 0) {
    PLOG(ERROR) << "Failed to set $KRB5CCNAME variable";
    return false;
  }
  return true;
}

bool SetKrb5TraceEnvironmentVariable() {
  if (setenv(kKrb5TraceEnvironmentVariable, GetKrb5TracePath().c_str(),
             1 /* overwrite */) != 0) {
    PLOG(ERROR) << "Failed to set $KRB5_TRACE variable";
    return false;
  }
  return true;
}

bool SetKerberosEnvironmentVariables() {
  return SetKrb5ConfigEnviornmentVariable() &&
         SetKrb5CCNameEnvironmentVariable() &&
         SetKrb5TraceEnvironmentVariable();
}

// Creates a directory at |path|. Logs and returns an error on failure.
bool CreateDirectory(const std::string& path) {
  base::File::Error ferror;
  if (!base::CreateDirectoryAndGetError(base::FilePath(path), &ferror)) {
    LOG(ERROR) << "Failed to create directory '" << path
               << "': " << base::File::ErrorToString(ferror);
    return false;
  }
  return true;
}

bool CreateKerberosDirectories() {
  return CreateDirectory(GetKrb5ConfLocation()) &&
         CreateDirectory(GetCCacheLocation()) &&
         CreateDirectory(GetKrb5TraceLocation());
}

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

// Creates smb configuration file in $HOME/.smb/smb.conf.
bool CreateSmbConfFile() {
  const std::string smb_conf_directory(std::string(kSmbProviderHome) +
                                       kSmbConfLocation);
  if (!CreateDirectory(smb_conf_directory)) {
    return false;
  }

  const int data_size = strlen(kSmbConfData);
  return base::WriteFile(base::FilePath(smb_conf_directory + kSmbConfFile),
                         kSmbConfData, data_size) == data_size;
}

}  // namespace

class SmbProviderDaemon : public brillo::DBusServiceDaemon {
 public:
  SmbProviderDaemon() : DBusServiceDaemon(kSmbProviderServiceName) {}

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    auto credential_store = std::make_unique<InMemoryCredentialStore>();
    std::unique_ptr<SambaInterfaceImpl> samba_interface =
        SambaInterfaceImpl::Create(base::Bind(
            base::IgnoreResult(&InMemoryCredentialStore::GetAuthentication),
            credential_store->AsWeakPtr()));
    if (!samba_interface) {
      LOG(ERROR) << "SambaInterface failed to initialize";
      exit(EXIT_FAILURE);
    }

    auto dbus_object = std::make_unique<brillo::dbus_utils::DBusObject>(
        nullptr, bus_, org::chromium::SmbProviderAdaptor::GetObjectPath());

    auto kerberos_artifact_client =
        std::make_unique<KerberosArtifactClient>(dbus_object.get());

    auto kerberos_artifact_synchronizer =
        std::make_unique<KerberosArtifactSynchronizer>(
            GetKrb5ConfPath(), GetCCachePath(),
            std::move(kerberos_artifact_client));

    auto mount_manager =
        std::make_unique<MountManager>(std::move(credential_store));

    smb_provider_ = std::make_unique<SmbProvider>(
        std::move(dbus_object), std::move(samba_interface),
        std::move(mount_manager), std::move(kerberos_artifact_synchronizer),
        false /* enable_metadata_cache */);
    smb_provider_->RegisterAsync(
        sequencer->GetHandler("SmbProvider.RegisterAsync() failed.", true));
  }

  void OnShutdown(int* return_code) override {
    DBusServiceDaemon::OnShutdown(return_code);
    smb_provider_.reset();
  }

 private:
  std::unique_ptr<SmbProvider> smb_provider_;
  DISALLOW_COPY_AND_ASSIGN(SmbProviderDaemon);
};

// Runs SmbProviderDaemon.
int RunDaemon() {
  SmbProviderDaemon daemon;
  int res = daemon.Run();
  LOG(INFO) << "smbproviderd stopping with exit code " << res;
  return res;
}

}  // namespace smbprovider

int main(int argc, char* argv[]) {
  smbprovider::InitLog();
  // Smb configuration file must be written before the daemon is started because
  // the check for smb.conf happens when the context is set.
  if (!(smbprovider::SetHomeEnvironmentVariable() &&
        smbprovider::SetKerberosEnvironmentVariables() &&
        smbprovider::CreateSmbConfFile() &&
        smbprovider::CreateKerberosDirectories())) {
    LOG(ERROR) << "Failed to set configuration files, exiting";
    return EXIT_FAILURE;
  }
  return smbprovider::RunDaemon();
}
