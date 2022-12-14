// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USERDATAAUTH_H_
#define CRYPTOHOME_USERDATAAUTH_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/location.h>
#include <base/task/single_thread_task_runner.h>
#include <base/threading/platform_thread.h>
#include <base/threading/thread.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <libhwsec/factory/factory.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/frontend/pinweaver/frontend.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libhwsec-foundation/status/status_chain_or.h>
#include <tpm_manager/client/tpm_manager_utility.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/auth_blocks/auth_block_utility.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_session.h"
#include "cryptohome/auth_session_manager.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/cleanup/low_disk_space_handler.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/fingerprint_manager.h"
#include "cryptohome/firmware_management_parameters.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/key_challenge_service_factory.h"
#include "cryptohome/key_challenge_service_factory_impl.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/pkcs11/pkcs11_token_factory.h"
#include "cryptohome/pkcs11_init.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/arc_disk_quota.h"
#include "cryptohome/storage/cryptohome_vault_factory.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_factory.h"
#include "cryptohome/user_secret_stash_storage.h"
#include "cryptohome/user_session/user_session.h"
#include "cryptohome/user_session/user_session_factory.h"
#include "cryptohome/user_session/user_session_map.h"
#include "cryptohome/uss_experiment_config_fetcher.h"

namespace cryptohome {

class UserDataAuth {
 public:
  struct MountArgs {
    // Whether to create the vault if it is missing.
    bool create_if_missing = false;
    // Whether the mount has to be ephemeral.
    bool is_ephemeral = false;
    // When creating a new cryptohome from scratch, use ecryptfs.
    bool create_as_ecryptfs = false;
    // Forces dircrypto, i.e., makes it an error to mount ecryptfs.
    bool force_dircrypto = false;
    // Enables version 2 fscrypt interface.
    bool enable_dircrypto_v2 = false;
    // Mount the existing ecryptfs vault to a temporary location while setting
    // up a new dircrypto directory.
    bool to_migrate_from_ecryptfs = false;
  };

  UserDataAuth();
  ~UserDataAuth();

  // Note that this function must be called from the thread that created this
  // object, so that |origin_task_runner_| is initialized correctly.
  bool Initialize();

  // This is the initialization function that is called after DBus is connected
  // and set_dbus() has been called. Usually Initialize() is called before this
  // method.
  bool PostDBusInitialize();

  // =============== Mount Related Public DBus API ===============
  // Methods below are used directly by the DBus interface

  // If username is empty, returns true if any mount is mounted, otherwise,
  // returns true if the mount associated with the given |username| is mounted.
  // For |is_ephemeral_out|, if no username is given, then is_ephemeral_out is
  // set to true when any mount is ephemeral. Otherwise, is_ephemeral_out is set
  // to true when the mount associated with the given |username| is mounted in
  // an ephemeral manner. If nullptr is passed in for is_ephemeral_out, then it
  // won't be touched. Ephemeral mount means that the content of the mount is
  // cleared once the user logs out.
  bool IsMounted(const std::string& username = "",
                 bool* is_ephemeral_out = nullptr);

  // Returns true if the mount that corresponds to the username is mounted,
  // false otherwise. If that mount is ephemeral, then is_ephemeral_out is set
  // to true, false otherwise. If nullptr is passed in for is_ephemeral_out,
  // then it won't be touched. Ephemeral mount means that the content of the
  // mount is cleared once the user logs out.
  bool IsMountedForUser(const std::string& username,
                        bool* is_ephemeral_out = nullptr);

  // Calling this function will unmount all mounted cryptohomes. It'll return
  // a reply without error if all mounts are cleanly unmounted.
  // Note: This must only be called on mount thread
  user_data_auth::UnmountReply Unmount();

  // This function will attempt to mount the requested user's home directory, as
  // specified in |request|. Once that's done, it'll call |on_done| to notify
  // the result. Note that there's no guarantee on whether |on_done| is called
  // before or after this function returns.
  void DoMount(
      user_data_auth::MountRequest request,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // Calling this method will kick start the migration to Dircrypto format (from
  // eCryptfs). |request| contains the account whose cryptohome to migrate, and
  // what whether minimal migration is to be performed. See definition of
  // message StartMigrateToDircryptoRequest for more information on minimal
  // migration. |progress_callback| is a callback that will be called whenever
  // there's progress update from the migration, or if the migration
  // completes/fails.
  void StartMigrateToDircrypto(
      const user_data_auth::StartMigrateToDircryptoRequest& request,
      Mount::MigrationCallback progress_callback);

  // Determine if the account specified by |account| needs to do Dircrypto
  // migration. Returns CRYPTOHOME_ERROR_NOT_SET if the query is successful, and
  // the result is stored in |result| (true for migration needed). Otherwise, an
  // error code is returned and result is in an undefined state.
  user_data_auth::CryptohomeErrorCode NeedsDircryptoMigration(
      const cryptohome::AccountIdentifier& account, bool* result);

  // Return the size of the user's home directory in number of bytes. If the
  // |account| given is non-existent, then 0 is returned.
  // Negative values are reserved for future cases whereby we need to do some
  // form of error reporting.
  int64_t GetAccountDiskUsage(const cryptohome::AccountIdentifier& account);

  // =============== Mount Related Public Utilities ===============

  // Called during initialization (and on mount events) to ensure old mounts
  // are marked for unmount when possible by the kernel.  Returns true if any
  // mounts were stale and not cleaned up (because of open files).
  // Note: This must only be called on mount thread
  //
  // Parameters
  // - force: if true, unmounts all existing shadow mounts.
  //          if false, unmounts shadows mounts with no open files.
  bool CleanUpStaleMounts(bool force);

  // Ensures the cryptohome keys had been loaded.
  void EnsureCryptohomeKeys();

  // Set the |force_ecryptfs_| variable, if true, all mounts will use eCryptfs
  // for encryption. If eCryptfs is not used, then dircrypto (the ext4
  // directory encryption mechanism) is used. Note that this is usually used in
  // main() because there's a command line switch for selecting dircrypto or
  // eCryptfs.
  void set_force_ecryptfs(bool force_ecryptfs) {
    force_ecryptfs_ = force_ecryptfs;
  }

  // Enable version 2 of fscrypt interface.
  void set_fscrypt_v2(bool enable_v2) { fscrypt_v2_ = enable_v2; }

  // Enable creating LVM volumes for applications.
  void set_enable_application_containers(bool value) {
    enable_application_containers_ = value;
  }

  // Set the |legacy_mount_| variable. For more information on legacy_mount_,
  // see comment of Mount::MountLegacyHome(). Note that this is usually used in
  // main() because there's a command line switch for selecting this.
  void set_legacy_mount(bool legacy) { legacy_mount_ = legacy; }

  // Set |bind_mount_downloads_|. The variable is passed to Mount to define
  // whether the Downloads/ directory shall be bind mounted.
  void set_bind_mount_downloads(bool bind) { bind_mount_downloads_ = bind; }

  // Set thresholds for automatic disk cleanup.
  void set_cleanup_threshold(uint64_t cleanup_threshold);
  void set_aggressive_cleanup_threshold(uint64_t aggressive_cleanup_threshold);
  void set_critical_cleanup_threshold(uint64_t critical_cleanup_threshold);
  void set_target_free_space(uint64_t target_free_space);

  // Set the |low_disk_space_callback_| variable. This is usually called by the
  // DBus adaptor.
  void SetLowDiskSpaceCallback(
      const base::RepeatingCallback<void(uint64_t)>& callback);

  // Set the FingerprintScanResult callback. This is usually called by the
  // DBus adaptor.
  void SetFingerprintScanResultCallback(
      const base::RepeatingCallback<
          void(user_data_auth::FingerprintScanResult)>& callback);

  // =============== Key Related Public Utilities ===============
  // Add the key specified in the request, and return a CryptohomeErrorCode to
  // indicate the status of adding the key. If CryptohomeErrorCode is
  // CRYPTOHOME_ERROR_NOT_SET, then the key is successfully added.
  user_data_auth::CryptohomeErrorCode AddKey(
      const user_data_auth::AddKeyRequest& request);

  // Check the key given in |request| again the currently mounted directories
  // and other credentials. |on_done| is called once the operation is completed,
  // and the error code is CRYPTOHOME_ERROR_NOT_SET if the key is found. Note
  // that this method is asynchronous.
  void CheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);

  // Remove the key given in |request.key| with the authorization given in
  // |request.authorization_request|. Returns CRYPTOHOME_ERROR_NOT_SET if the
  // key is successfully removed, returns other Error Code if not.
  user_data_auth::CryptohomeErrorCode RemoveKey(
      const user_data_auth::RemoveKeyRequest request);

  // Remove all keys under the account specified by |request.account_id| except
  // those listed in |request.exempt_key_data| (only the label).
  // |request.authorization_request| contains the authorization to authorize
  // this change. Returns CRYPTOHOME_ERROR_NOT_SET if the operation is
  // successful.
  user_data_auth::CryptohomeErrorCode MassRemoveKeys(
      const user_data_auth::MassRemoveKeysRequest request);

  // List the keys stored in |homedirs_|.
  // See definition of ListKeysReply for what is returned.
  user_data_auth::ListKeysReply ListKeys(
      const user_data_auth::ListKeysRequest& request);

  // Get the KeyData associated with key that have the label specified in
  // |request.key.data.label|. If there's an error processing this request, then
  // an error code will be returned, and |found|/|data_out| will be untouched.
  // If there's no key with the lable found, then no error will be returned, and
  // |found| will be set to false. Otherwise, the key's keydata will be written
  // to |data_out| and |found| will be set to true. Note that KeyData is
  // actually the key's metadata.
  user_data_auth::CryptohomeErrorCode GetKeyData(
      const user_data_auth::GetKeyDataRequest& request,
      cryptohome::KeyData* data_out,
      bool* found);

  // This will migrate, or rather say, change the underlying secret that is used
  // to protect the user's home directory. The home directory key to change is
  // specified by |request.account_id|, the change is authorized by
  // |request.authorization_request| and the new secret is specified by
  // |request.secret|. This function will return CRYPTOHOME_ERROR_NOT_SET if the
  // operation is successful, and other error code if it failed.
  user_data_auth::CryptohomeErrorCode MigrateKey(
      const user_data_auth::MigrateKeyRequest& request);

  // Remove the cryptohome (user's home directory) specified in
  // |request.identifier|. See definition of RemoveReply for what is returned.
  user_data_auth::RemoveReply Remove(
      const user_data_auth::RemoveRequest& request);

  // Reset the application container specified in the request for the user
  // identified by authsession id.
  user_data_auth::ResetApplicationContainerReply ResetApplicationContainer(
      const user_data_auth::ResetApplicationContainerRequest& request);

  // Return true if we support low entropy credential.
  bool IsLowEntropyCredentialSupported();

  // =============== ARC Quota Related Public Methods ===============

  // Return true is ARC Disk Quota is supported, false otherwise.
  bool IsArcQuotaSupported();

  // Return the current disk usage for an android uid (a shifted uid) in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcUid(uid_t android_uid);

  // Return the current disk usage for an android gid (a shifted gid) in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcGid(uid_t android_gid);

  // Return the current disk usage for an android project id in bytes.
  // Will return a negative number if the request fails. See
  // cryptohome/arc_disk_quota.h for more details.
  int64_t GetCurrentSpaceForArcProjectId(int project_id);

  // Sets the project ID of a media_rw_data_file.
  // See cryptohome/arc_disk_quota.h for more details.
  bool SetMediaRWDataFileProjectId(int project_id, int fd, int* out_error);

  // Sets the project inheritance flag of a media_rw_data_file.
  // See cryptohome/arc_disk_quota.h for more details.
  bool SetMediaRWDataFileProjectInheritanceFlag(bool enable,
                                                int fd,
                                                int* out_error);

  // =============== PKCS#11 Related Public Methods ===============

  // Returns true if and only if PKCS#11 tokens are ready for all mounts.
  bool Pkcs11IsTpmTokenReady();

  // Return the information regarding a token. If username is empty, then system
  // token's information is given. Otherwise, the corresponding user token
  // information is given. Note that this function doesn't check if the given
  // username is valid or not. If a non-existent user is given, then the result
  // is undefined.
  // Note that if this method fails to get the slot associated with the token,
  // then -1 will be supplied for slot.
  user_data_auth::TpmTokenInfo Pkcs11GetTpmTokenInfo(
      const std::string& username);

  // Calling this method will remove PKCS#11 tokens on all mounts.
  // Note that this should only be called from mount thread.
  void Pkcs11Terminate();

  // Calling this method will restore all the tokens to chaps.
  // Note that this should only be called from mount thread.
  void Pkcs11RestoreTpmTokens();

  // =============== Install Attributes Related Public Methods ===============

  // Retrieve the key value pair in install attributes with the key of |name|,
  // and return its value in |data_out|. Returns true if and only if the key
  // value pair is successfully retrieved. If false is returned, then
  // |data_out|'s content is undefined.
  bool InstallAttributesGet(const std::string& name,
                            std::vector<uint8_t>* data_out);

  // Insert the key value pair (name, data) into install attributes. Return true
  // if and only if the key value pair is successfully inserted.
  bool InstallAttributesSet(const std::string& name,
                            const std::vector<uint8_t>& data);

  // Finalize the install attributes. Return true if and only if the install
  // attributes is finalized.
  bool InstallAttributesFinalize();

  // Get the number of key value pair stored in install attributes.
  int InstallAttributesCount();

  // Return true if and only if the attribute storage is securely stored, that
  // is, if the system TPM/Lockbox is being used.
  bool InstallAttributesIsSecure();

  // Return the current status of the install attributes.
  InstallAttributes::Status InstallAttributesGetStatus();

  // Convert the InstallAttributes::Status enum to
  // user_data_auth::InstallAttributesState protobuf enum.
  static user_data_auth::InstallAttributesState
  InstallAttributesStatusToProtoEnum(InstallAttributes::Status status);

  // =============== Install Attributes Related Utilities ===============

  // Return true if this device is enterprise owned.
  bool IsEnterpriseOwned() {
    AssertOnMountThread();
    return enterprise_owned_;
  }

  // ============= Fingerprint Auth Related Public Methods ==============

  // Start fingerprint auth session asynchronously for the user specified in
  // |request|, and call |on_done|.
  void StartFingerprintAuthSession(
      const user_data_auth::StartFingerprintAuthSessionRequest& request,
      base::OnceCallback<void(
          const user_data_auth::StartFingerprintAuthSessionReply&)> on_done);

  // End the current fingerprint auth session.
  void EndFingerprintAuthSession();

  // TODO(b/184393647): This api is not currently used because secret
  // enforcement in the WebAuthn flow haven't been implemented yet. After
  // implemented, u2fd calls this api to retrieve the WebAuthn secret to use in
  // the sign command.
  user_data_auth::GetWebAuthnSecretReply GetWebAuthnSecret(
      const user_data_auth::GetWebAuthnSecretRequest& request);

  user_data_auth::GetWebAuthnSecretHashReply GetWebAuthnSecretHash(
      const user_data_auth::GetWebAuthnSecretHashRequest& request);

  // =============  Hibernate Secret Public Methods ==============
  user_data_auth::GetHibernateSecretReply GetHibernateSecret(
      const user_data_auth::GetHibernateSecretRequest& request);

  // Retrieves information on what encryption features are in use in cryptohome,
  // such as Intel Keylocker. This allows other services such as hiberate
  // manager to determine treatments needed for when these features are enabled.
  user_data_auth::GetEncryptionInfoReply GetEncryptionInfo(
      const user_data_auth::GetEncryptionInfoRequest& request);

  // ========= Firmware Management Parameters Related Public Methods =========

  // Retrieve the firmware management parameters. Returns
  // CRYPTOHOME_ERROR_NOT_SET if successful, and in that case, |fwmp| will be
  // filled with the firmware management parameters. Otherwise, an error code is
  // returned and |fwmp|'s content is undefined.
  user_data_auth::CryptohomeErrorCode GetFirmwareManagementParameters(
      user_data_auth::FirmwareManagementParameters* fwmp);

  // Set the firmware management parameters to the value given in |fwmp|.
  // Returns CRYPTOHOME_ERROR_NOT_SET if the operation is successful, and other
  // error code if it failed.
  user_data_auth::CryptohomeErrorCode SetFirmwareManagementParameters(
      const user_data_auth::FirmwareManagementParameters& fwmp);

  // Remove the firmware management parameters, that is, undefine its NVRAM
  // space (if defined). Return true if and only if the firmware management
  // parameters are gone
  bool RemoveFirmwareManagementParameters();

  // =============== Miscellaneous Public APIs ===============

  // Retrieve the current system salt. This method call is always successful.
  // Note that this should never be called before Initialize() is successful,
  // otherwise an assertion will fail.
  const brillo::SecureBlob& GetSystemSalt();

  // Update the current user activity timestamp for all mounts. time_shift_sec
  // is the time, expressed in number of seconds since the last user activity.
  // For instance, if the unix timestamp now is x, if this value is 5, then the
  // last user activity happened at x-5 unix timestamp.
  // This method will return true if the update is successful for all mounts.
  // Note that negative |time_shift_sec| values are reserved and should not be
  // used.
  bool UpdateCurrentUserActivityTimestamp(int time_shift_sec);

  // Calling this method will prevent another user from logging in later by
  // extending PCR, causing PCR-bound VKKs to be inaccessible. This is used by
  // ARC++. |account_id| contains the user that we'll lock to before reboot.
  user_data_auth::CryptohomeErrorCode LockToSingleUserMountUntilReboot(
      const cryptohome::AccountIdentifier& account_id);

  // Retrieve the RSU Device ID, return true if and only if |rsu_device_id| is
  // set to the RSU Device ID.
  bool GetRsuDeviceId(std::string* rsu_device_id);

  // Return true iff powerwash is required. i.e. cannot unseal with user auth.
  bool RequiresPowerwash();

  // Returns true if and only if the loaded device policy specifies an owner
  // user.
  bool OwnerUserExists();

  // Get a JSON string that represents various state of this service.
  // Note: This can only be called on mount thread.
  std::string GetStatusString();

  // =============== Miscellaneous ===============

  // This is called by OwnershipCallback when there's any update on ownership
  // status of the TPM.
  // Note: This can only be called on mount thread.
  void OwnershipCallback(bool status, bool took_ownership);

  // Set the current dbus connection, this is usually used by the dbus daemon
  // object that owns the instance of this object. During testing, the test
  // fixture might call this for dependency injection.
  void set_dbus(scoped_refptr<::dbus::Bus> bus) { bus_ = bus; }

  // Set the current dbus connection for mount thread, this is usually used by
  // the dbus daemon object that owns the instance of this object. During
  // testing, the test fixture might call this for dependency injection.
  // Note that it is the responsibility of the caller to cleanup/destroy the Bus
  // object during destruction.
  void set_mount_thread_dbus(scoped_refptr<::dbus::Bus> bus) {
    mount_thread_bus_ = bus;
  }

  // ================= Threading Utilities ==================

  // Returns true if we are currently running on the origin thread
  bool IsOnOriginThread() const {
    // Note that this function should not solely rely on |origin_task_runner_|
    // because it may be unavailable when this function is first called by
    // UserDataAuth::Initialize()
    if (origin_task_runner_) {
      return origin_task_runner_->RunsTasksInCurrentSequence();
    }
    return base::PlatformThread::CurrentId() == origin_thread_id_;
  }

  // Returns true if we are currently running on the mount thread
  bool IsOnMountThread() const {
    if (mount_task_runner_) {
      return mount_task_runner_->RunsTasksInCurrentSequence();
    }
    // GetThreadId blocks if the thread is not started yet.
    return mount_thread_->IsRunning() &&
           base::PlatformThread::CurrentId() == mount_thread_->GetThreadId();
  }

  // DCHECK if we are running on the origin thread. Will have no effect
  // in production.
  void AssertOnOriginThread() const { DCHECK(IsOnOriginThread()); }

  // DCHECK if we are running on the mount thread. Will have no effect
  // in production.
  void AssertOnMountThread() const { DCHECK(IsOnMountThread()); }

  // Post Task to origin thread. For the caller, from_here is usually FROM_HERE
  // macro, while task is a callback function to be posted. Will return true if
  // the task may be run sometime in the future, false if it will definitely not
  // run. Specify |delay| if you want the task to be deferred for |delay| amount
  // of time.
  bool PostTaskToOriginThread(const base::Location& from_here,
                              base::OnceClosure task,
                              const base::TimeDelta& delay = base::TimeDelta());

  // Post Task to mount thread. For the caller, from_here is usually FROM_HERE
  // macro, while task is a callback function to be posted. Will return true if
  // the task may be run sometime in the future, false if it will definitely not
  // run. Specify |delay| if you want the task to be deferred for |delay| amount
  // of time.
  bool PostTaskToMountThread(const base::Location& from_here,
                             base::OnceClosure task,
                             const base::TimeDelta& delay = base::TimeDelta());

  // ================= Testing Utilities ==================
  // Note that all functions below in this section should only be used for unit
  // testing purpose only.

  // Override |crypto_| for testing purpose
  void set_crypto(cryptohome::Crypto* crypto) { crypto_ = crypto; }

  // Override |keyset_management_| for testing purpose
  void set_keyset_management(KeysetManagement* value) {
    keyset_management_ = value;
  }

  // Override |keyset_management_| for testing purpose
  void set_auth_block_utility(AuthBlockUtility* value) {
    auth_block_utility_ = value;
  }

  // Override |auth_factor_manager_| for testing purpose
  void set_auth_factor_manager_for_testing(AuthFactorManager* value) {
    auth_factor_manager_ = value;
  }

  // Override |user_secret_stash_storage_| for testing purpose
  void set_user_secret_stash_storage_for_testing(
      UserSecretStashStorage* value) {
    user_secret_stash_storage_ = value;
  }

  void set_user_session_map_for_testing(UserSessionMap* user_session_map) {
    sessions_ = user_session_map;
  }

  // Override |auth_session_manager_| for testing purpose
  void set_auth_session_manager(AuthSessionManager* value) {
    auth_session_manager_ = value;
  }

  void set_user_activity_timestamp_manager(
      UserOldestActivityTimestampManager* user_activity_timestamp_manager) {
    user_activity_timestamp_manager_ = user_activity_timestamp_manager;
  }

  // Override |vault_factory_| for testing purpose
  void set_vault_factory_for_testing(CryptohomeVaultFactory* vault_factory) {
    vault_factory_ = vault_factory;
  }

  // Override |homedirs_| for testing purpose
  void set_homedirs(cryptohome::HomeDirs* homedirs) { homedirs_ = homedirs; }

  // Override |hwsec_factory_| for testing purpose
  void set_hwsec_factory(hwsec::Factory* hwsec_factory) {
    hwsec_factory_ = hwsec_factory;
  }

  // Override |hwsec_| for testing purpose
  void set_hwsec(hwsec::CryptohomeFrontend* hwsec) { hwsec_ = hwsec; }

  // Override |pinweaver_| for testing purpose
  void set_pinweaver(hwsec::PinWeaverFrontend* pinweaver) {
    pinweaver_ = pinweaver;
  }

  // Override |recovery_crypto| for testing purpose
  void set_recovery_crypto(hwsec::RecoveryCryptoFrontend* recovery_crypto) {
    recovery_crypto_ = recovery_crypto;
  }

  // Override |cryptohome_keys_manager_| for testing purpose
  void set_cryptohome_keys_manager(
      CryptohomeKeysManager* cryptohome_keys_manager) {
    cryptohome_keys_manager_ = cryptohome_keys_manager;
  }

  // Override |tpm_manager_util_| for testing purpose
  void set_tpm_manager_util_(tpm_manager::TpmManagerUtility* tpm_manager_util) {
    tpm_manager_util_ = tpm_manager_util;
  }

  // Override |platform_| for testing purpose
  void set_platform(cryptohome::Platform* platform) { platform_ = platform; }

  // override |chaps_client_| for testing purpose
  void set_chaps_client(chaps::TokenManagerClient* chaps_client) {
    chaps_client_ = chaps_client;
  }

  // Override |install_attrs_| for testing purpose
  void set_install_attrs(InstallAttributes* install_attrs) {
    install_attrs_ = install_attrs;
  }

  // Override |arc_disk_quota_| for testing purpose
  void set_arc_disk_quota(cryptohome::ArcDiskQuota* arc_disk_quota) {
    arc_disk_quota_ = arc_disk_quota;
  }

  // Override |pkcs11_init_| for testing purpose
  void set_pkcs11_init(Pkcs11Init* pkcs11_init) { pkcs11_init_ = pkcs11_init; }

  // Override |pkcs11_token_factory_| for testing purpose
  void set_pkcs11_token_factory(Pkcs11TokenFactory* pkcs11_token_factory) {
    pkcs11_token_factory_ = pkcs11_token_factory;
  }

  // Override |firmware_management_parameters_| for testing purpose
  void set_firmware_management_parameters(FirmwareManagementParameters* fwmp) {
    firmware_management_parameters_ = fwmp;
  }

  // Override |fingerprint_manager_| for testing purpose
  void set_fingerprint_manager(FingerprintManager* fingerprint_manager) {
    fingerprint_manager_ = fingerprint_manager;
  }

  // Override |uss_experiment_config_fetcher_| for testing purpose
  void set_uss_experiment_config_fetcher(
      UssExperimentConfigFetcher* uss_experiment_config_fetcher) {
    uss_experiment_config_fetcher_ = uss_experiment_config_fetcher;
  }

  // Override |mount_factory_| for testing purpose
  void set_mount_factory_for_testing(MountFactory* mount_factory) {
    mount_factory_ = mount_factory;
  }

  // Override |user_session_factory_| for testing purpose
  void set_user_session_factory(UserSessionFactory* user_session_factory) {
    user_session_factory_ = user_session_factory;
  }

  // Override |challenge_credentials_helper_| for testing purpose
  void set_challenge_credentials_helper(
      ChallengeCredentialsHelper* challenge_credentials_helper) {
    challenge_credentials_helper_ = challenge_credentials_helper;
  }

  // Override |key_challenge_service_factory_| for testing purpose
  void set_key_challenge_service_factory(
      KeyChallengeServiceFactory* key_challenge_service_factory) {
    key_challenge_service_factory_ = key_challenge_service_factory;
  }

  // Override |origin_task_runner_| for testing purpose
  void set_origin_task_runner(
      scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner) {
    origin_task_runner_ = origin_task_runner;
  }

  // Override |mount_task_runner_| for testing purpose
  void set_mount_task_runner(
      scoped_refptr<base::SingleThreadTaskRunner> mount_task_runner) {
    mount_task_runner_ = mount_task_runner;
  }

  // Override |low_disk_space_handler_| for testing purpose
  void set_low_disk_space_handler(LowDiskSpaceHandler* low_disk_space_handler) {
    low_disk_space_handler_ = low_disk_space_handler;
  }

  // Retrieve the session associated with the given user, for testing purpose
  // only.
  UserSession* FindUserSessionForTest(const std::string& username) {
    return sessions_->Find(username);
  }

  // Associate a particular session object |session| with the username
  // |username| for testing purpose
  bool AddUserSessionForTest(const std::string& username,
                             std::unique_ptr<UserSession> session) {
    return sessions_->Add(username, std::move(session));
  }

  void StartAuthSession(
      user_data_auth::StartAuthSessionRequest request,
      base::OnceCallback<void(const user_data_auth::StartAuthSessionReply&)>
          on_done);

  void AddCredentials(
      user_data_auth::AddCredentialsRequest request,
      base::OnceCallback<void(const user_data_auth::AddCredentialsReply&)>
          on_done);

  void UpdateCredential(
      user_data_auth::UpdateCredentialRequest request,
      base::OnceCallback<void(const user_data_auth::UpdateCredentialReply&)>
          on_done);

  void AuthenticateAuthSession(
      user_data_auth::AuthenticateAuthSessionRequest request,
      base::OnceCallback<
          void(const user_data_auth::AuthenticateAuthSessionReply&)> on_done);

  void InvalidateAuthSession(
      user_data_auth::InvalidateAuthSessionRequest request,
      base::OnceCallback<
          void(const user_data_auth::InvalidateAuthSessionReply&)> on_done);

  void ExtendAuthSession(
      user_data_auth::ExtendAuthSessionRequest request,
      base::OnceCallback<void(const user_data_auth::ExtendAuthSessionReply&)>
          on_done);

  void PrepareGuestVault(
      user_data_auth::PrepareGuestVaultRequest request,
      base::OnceCallback<void(const user_data_auth::PrepareGuestVaultReply&)>
          on_done);

  void PrepareEphemeralVault(
      user_data_auth::PrepareEphemeralVaultRequest request,
      base::OnceCallback<
          void(const user_data_auth::PrepareEphemeralVaultReply&)> on_done);

  void PreparePersistentVault(
      user_data_auth::PreparePersistentVaultRequest request,
      base::OnceCallback<
          void(const user_data_auth::PreparePersistentVaultReply&)> on_done);

  void PrepareVaultForMigration(
      user_data_auth::PrepareVaultForMigrationRequest request,
      base::OnceCallback<
          void(const user_data_auth::PrepareVaultForMigrationReply&)> on_done);

  void CreatePersistentUser(
      user_data_auth::CreatePersistentUserRequest request,
      base::OnceCallback<void(const user_data_auth::CreatePersistentUserReply&)>
          on_done);

  void AddAuthFactor(
      user_data_auth::AddAuthFactorRequest request,
      base::OnceCallback<void(const user_data_auth::AddAuthFactorReply&)>
          on_done);

  void AuthenticateAuthFactor(
      user_data_auth::AuthenticateAuthFactorRequest request,
      base::OnceCallback<
          void(const user_data_auth::AuthenticateAuthFactorReply&)> on_done);

  void UpdateAuthFactor(
      user_data_auth::UpdateAuthFactorRequest request,
      base::OnceCallback<void(const user_data_auth::UpdateAuthFactorReply&)>
          on_done);

  void RemoveAuthFactor(
      user_data_auth::RemoveAuthFactorRequest request,
      base::OnceCallback<void(const user_data_auth::RemoveAuthFactorReply&)>
          on_done);

  void ListAuthFactors(
      user_data_auth::ListAuthFactorsRequest request,
      base::OnceCallback<void(const user_data_auth::ListAuthFactorsReply&)>
          on_done);

  void GetAuthFactorExtendedInfo(
      user_data_auth::GetAuthFactorExtendedInfoRequest request,
      base::OnceCallback<
          void(const user_data_auth::GetAuthFactorExtendedInfoReply&)> on_done);

  void PrepareAuthFactor(
      user_data_auth::PrepareAuthFactorRequest request,
      base::OnceCallback<void(const user_data_auth::PrepareAuthFactorReply&)>
          on_done);

  void TerminateAuthFactor(
      user_data_auth::TerminateAuthFactorRequest request,
      base::OnceCallback<void(const user_data_auth::TerminateAuthFactorReply&)>
          on_done);

  void GetAuthSessionStatus(
      user_data_auth::GetAuthSessionStatusRequest request,
      base::OnceCallback<void(const user_data_auth::GetAuthSessionStatusReply&)>
          on_done);

  void GetRecoveryRequest(
      user_data_auth::GetRecoveryRequestRequest request,
      base::OnceCallback<void(const user_data_auth::GetRecoveryRequestReply&)>
          on_done);

 private:
  // base::Thread subclass so we can implement CleanUp.
  class MountThread : public base::Thread {
   public:
    explicit MountThread(const std::string& name, UserDataAuth* uda)
        : base::Thread(name), uda_(uda) {
      CHECK(uda_);
    }
    MountThread(const MountThread&) = delete;
    MountThread& operator=(const MountThread&) = delete;

    ~MountThread() override { Stop(); }

   private:
    void CleanUp() override { uda_->ShutdownTask(); }

    UserDataAuth* const uda_;
  };

  // Shutdown to be run on the worker thread.
  void ShutdownTask();

  // This create a dbus connection whose origin thread is UserDataAuth's mount
  // thread.
  void CreateMountThreadDBus();

  // =============== Mount Related Utilities ===============

  // Performs a single attempt to Mount a non-annonimous user.
  MountStatus AttemptUserMount(const Credentials& credentials,
                               const MountArgs& mount_args,
                               UserSession* user_session);

  // Performs a single attempt to Mount a non-annonimous user with AuthSession
  MountStatus AttemptUserMount(AuthSession* auth_session,
                               const MountArgs& mount_args,
                               UserSession* user_session);

  // Filters out active mounts from |mounts|, populating |active_mounts| set.
  // If |include_busy_mount| is false, then stale mounts with open files and
  // mount points connected to children of the mount source will be treated as
  // active mount, and be moved from |mounts| to |active_mounts|. Otherwise, all
  // stale mounts are included in |mounts|. Returns true if |include_busy_mount|
  // is true and there's at least one stale mount with open file(s) and treated
  // as active mount during the process.
  bool FilterActiveMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts,
      std::multimap<const base::FilePath, const base::FilePath>* active_mounts,
      bool include_busy_mount);

  // Populates |mounts| with ephemeral cryptohome mount points.
  void GetEphemeralLoopDevicesMounts(
      std::multimap<const base::FilePath, const base::FilePath>* mounts);

  // Unload any user pkcs11 tokens _not_ belonging to one of the mounts in
  // |exclude|. This is used to clean up any stale loaded tokens after a
  // cryptohome crash.
  // Note that system tokens are not affected.
  bool UnloadPkcs11Tokens(const std::vector<base::FilePath>& exclude);

  // Safely empties the UserSessionMap and may requests unmounting.
  // Note: This must only be called on mount thread
  bool RemoveAllMounts();

  // Calling this function will try to ensure that |public_mount_salt_| is ready
  // to use. If it's not ready, we'll generate it. Returns true if
  // |public_mount_salt_| is ready.
  bool CreatePublicMountSaltIfNeeded();

  // Gets passkey for |public_mount_id|. Returns true if a passkey is generated
  // successfully. Otherwise, returns false.
  bool GetPublicMountPassKey(const std::string& public_mount_id,
                             std::string* public_mount_passkey);

  // Determines whether the mount request should be ephemeral. On error, returns
  // status, otherwise, return the result (whether to mount ephemeral).
  CryptohomeStatusOr<bool> GetShouldMountAsEphemeral(
      const std::string& account_id,
      bool is_ephemeral_mount_requested,
      bool has_create_request) const;

  // Returns either an existing or a newly created UserSession, if not present.
  UserSession* GetOrCreateUserSession(const std::string& username);

  // Calling this method will mount the home directory for guest users.
  // This is usually called by DoMount(). Note that this method is asynchronous,
  // and will call |on_done| exactly once to deliver the result regardless of
  // whether the operation is successful.
  void MountGuest(
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // Performs the lazy part of the initialization that is required for
  // performing operations with challenge-response keys. Returns whether
  // succeeded.
  CryptohomeStatus InitForChallengeResponseAuth();

  // This is a utility function used by DoMount(). It is called if the request
  // mounting operation requires challenge response authentication. i.e. The key
  // for the storage is sealed.
  void DoChallengeResponseMount(
      const user_data_auth::MountRequest& request,
      const MountArgs& mount_args,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // This is a utility function used by DoChallengeResponseMount(), and is
  // called once we're done doing challenge response authentication.
  void OnChallengeResponseMountCredentialsObtained(
      const user_data_auth::MountRequest& request,
      const MountArgs mount_args,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done,
      TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
          result);

  // This is a utility function used by DoMount(). It is called either by
  // DoMount() (when using password for authentication.), or by
  // OnChallengeResponseMountCredentialsObtained() (when using challenge
  // response authentication.).
  void ContinueMountWithCredentials(
      const user_data_auth::MountRequest& request,
      std::unique_ptr<Credentials> credentials,
      std::optional<base::UnguessableToken> token,
      const MountArgs& mount_args,
      base::OnceCallback<void(const user_data_auth::MountReply&)> on_done);

  // Called on Mount thread. This triggers the credentials verification steps
  // that are specific to challenge-response keys, going through
  // {TryLightweightChallengeResponseCheckKeyEx(),
  // OnLightweightChallengeResponseCheckKeyExDone()} and/or
  // {DoFullChallengeResponseCheckKeyEx(),
  // OnFullChallengeResponseCheckKeyExDone()}.
  void DoChallengeResponseCheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);
  void TryLightweightChallengeResponseCheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);
  void OnLightweightChallengeResponseCheckKeyDone(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
      TPMStatus status);
  void DoFullChallengeResponseCheckKey(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done);
  void OnFullChallengeResponseCheckKeyDone(
      const user_data_auth::CheckKeyRequest& request,
      base::OnceCallback<void(user_data_auth::CryptohomeErrorCode)> on_done,
      TPMStatusOr<ChallengeCredentialsHelper::GenerateNewOrDecryptResult>
          result);

  // Called on Mount Thread, initializes the challenge_credentials_helper_
  // and the key_challenge_service_factory_, and forwards these
  // arguments to AuthBlockUtility.
  void InitializeChallengeCredentialsHelper();

  void GetAuthSessionStatusImpl(
      AuthSession* auth_session,
      user_data_auth::GetAuthSessionStatusReply& reply);

  // ================= Key Management Related Helper Methods ============

  // This utility function loads the user vault keyset. Add the vault keyset
  // first, if the user is a new user.
  MountStatusOr<std::unique_ptr<VaultKeyset>> LoadVaultKeyset(
      const Credentials& credentials, bool is_new_user);

  // This utility function adds a new credential to the user keyset. Obtains the
  // existing vault keyset by authenticating with the existing credentials; adds
  // a reset seed to the existing vault keyset; and then adds the new
  // credentials and key data to the user vault keyset.
  CryptohomeErrorCode AddVaultKeyset(const Credentials& existing_credentials,
                                     const Credentials& new_credentials,
                                     bool clobber);

  //  This utility function wraps the keyset_management methods to migrate to a
  //  new credential for the user keyset.Obtains the vault keyset by
  //  authenticating against the existing credentials, and calls Migrate() with
  //  this vault keyset and the given new credentials.
  bool MigrateVaultKeyset(const Credentials& existing_credentials,
                          const Credentials& new_credentials);

  // ================ Fingerprint Auth Related Methods ==================

  // Called on Mount thread. This creates a dbus proxy for Biometrics Daemon
  // and connects to signals.
  void CreateFingerprintManager();

  // Called on Mount thread. The returns a pointer to the fingerprint manager,
  // or null if it has not been created.
  FingerprintManager* GetFingerprintManager() const;

  // Called on Mount thread when fingerprint auth session starts or fails to
  // start.
  void OnFingerprintStartAuthSessionResp(
      base::OnceCallback<void(
          const user_data_auth::StartFingerprintAuthSessionReply&)> on_done,
      bool success);

  // Called on Mount thread. Scheduled by CheckKey(). Callback for one
  // fingerprint scan. Completes fingerprint CheckKey by running |on_done|.
  void CompleteFingerprintCheckKey(
      base::OnceCallback<void(const user_data_auth::CryptohomeErrorCode)>
          on_done,
      FingerprintScanStatus status);

  // OnFingerprintScanResult will be called on every received fingerprint
  // scan result. It will forward results to
  // |fingerprint_scan_result_callback_|.
  void OnFingerprintScanResult(user_data_auth::FingerprintScanResult result);

  // =============== Periodic Maintenance Related Methods ===============

  // Called periodically on Mount thread to detect low disk space and emit a
  // signal if detected. This also does various cleanup task.
  void LowDiskCallback();

  // This is called periodically, and does some routine cleanups. Currently this
  // free disk space consumed by unused cryptohomes and reset the dictionary
  // attack mitigation.
  void DoAutoCleanup();

  // Does what its name suggests. Usually called by DoAutoCleanup().
  void ResetDictionaryAttackMitigation();

  // This method takes entropy from the TPM and seeds it to /dev/urandom.
  void SeedUrandom();

  // =============== PKCS#11 Related Utilities ===============

  // This initializes the PKCS#11 for a particular mount. Note that this is
  // used mostly internally, by Mount related functions to bring up the PKCS#11
  // functionalities after mounting.
  void InitializePkcs11(UserSession* mount);

  // =============== Install Attributes Related Utilities ===============

  // Set whether this device is enterprise owned. Calling this method will have
  // effect on all currently mounted mounts. This can only be called on
  // mount_thread_.
  void SetEnterpriseOwned(bool enterprise_owned);

  // Detect whether this device is enterprise owned, and call
  // SetEnterpriseOwned(). This can only be called on origin thread.
  void DetectEnterpriseOwnership();

  // Call this method to initialize the install attributes functionality. This
  // can only be called on origin thread.
  void InitializeInstallAttributes();

  // =============== Signal Related Utilities/Callback ===============

  // This is called whenever the signal handlers for signals on tpm_manager's
  // interface is registered/connected. This is only used by the signal handler
  // registration function in tpm_manager's generated D-Bus proxy.
  void OnTpmManagerSignalConnected(const std::string& interface,
                                   const std::string& signal,
                                   bool success);

  // This is called whenever the OwnershipTaken signal is emitted by
  // tpm_manager.
  // Note: The caller of it may neither origin thread nor mount thread.
  void OnOwnershipTakenSignal();

  // =============== Stateful Recovery related Helpers ===============

  // This is a utility function for stateful recovery functionality to call when
  // it wants to mount a user's home directory. The user specified by
  // |username|'s home is mounted with |passkey|, and if successfully mounted,
  // the function returns true and set |out_home_path| to the mounted home.
  // Otherwise, return false. Note that this function must log any error itself,
  // no logging will be done by the caller.
  bool StatefulRecoveryMount(const std::string& username,
                             const std::string& passkey,
                             base::FilePath* out_home_path);

  // This is a utility function for stateful recovery to unmount all user's home
  // directories. It'll return true if all the user's home directories are
  // successfully unmounted. Otherwise, it'll return false. Note that this
  // function must log any error itself, no logging will be done by the caller.
  bool StatefulRecoveryUnmount();

  // This is a utility function for stateful recovery to check if a user is the
  // owner. It'll return true if the user specified by |username| is the owner,
  // and false otherwise.
  bool StatefulRecoveryIsOwner(const std::string& username);

  // Ensures BootLockbox is finalized;
  void EnsureBootLockboxFinalized();

  // =============== Auth Session Related Helpers ===============

  // The method takes serialized auth session id and returns an authenticated
  // auth session associated with the id. If the session is missing or not
  // authenticated, an error status is returned. The returned pointer is owner
  // by |auth_session_manager|.
  CryptohomeStatusOr<AuthSession*> GetAuthenticatedAuthSession(
      const std::string& auth_session_id);

  // Returns sanitized username for an existing auth session or an empty string
  // if the session wasn't found.
  std::string SanitizedUserNameForSession(const std::string& auth_session_id);

  // Returns a reference to the user session, if the session is mountable. The
  // session is mountable if it is not already mounted, and the guest is not
  // mounted. If user session object doesn't exist, this method will create
  // one.
  CryptohomeStatusOr<UserSession*> GetMountableUserSession(
      AuthSession* auth_session);

  // Pre-mount hook specifies operations that need to be executed before doing
  // mount. Eventually those actions should be triggered outside of mount code.
  // Not applicable to guest user.
  void PreMountHook(const std::string& obfuscated_username);

  // Post-mount hook specifies operations that need to be executed after doing
  // mount. Eventually those actions should be triggered outside of mount code.
  // Not applicable to guest user.
  void PostMountHook(UserSession* user_session, const MountStatus& error);

  // Converts the Dbus value for encryption type into internal representation.
  EncryptedContainerType DbusEncryptionTypeToContainerType(
      user_data_auth::VaultEncryptionType type);

  // The following methods are implementations for the DBus endpoints of the
  // new API. They are split from the actual end-points to simplify unit
  // testing. The E2E test of the calls is done in tast.

  CryptohomeStatus PrepareGuestVaultImpl();

  CryptohomeStatus PrepareEphemeralVaultImpl(
      const std::string& auth_session_id);

  CryptohomeStatus PreparePersistentVaultImpl(
      const std::string& auth_session_id,
      const CryptohomeVault::Options& vault_options);

  CryptohomeStatus CreatePersistentUserImpl(const std::string& auth_session_id);

  // Handled add credentials for ephemeral users. This function does not save
  // anything to the disk and just sets verifier in memory for screen unlock.
  user_data_auth::CryptohomeErrorCode HandleAddCredentialForEphemeralVault(
      AuthorizationRequest request, const AuthSession* auth_session);

  // OnAddCredentialFinished when AuthSession::AddCredential is finished. The
  // function will set credential in user_session for user to do unlock. The
  // credential verifier will not be overridden if it is already set once.
  void OnAddCredentialFinished(AuthSession* auth_session,
                               StatusCallback on_done,
                               CryptohomeStatus status);

  // OnUpdateCredentialFinished is called when AuthSession::UpdateCredentials is
  // finished. The function will set credential in user_session for user to do
  // unlock. The credential verifier will be overridden if it is already set
  // once.
  void OnUpdateCredentialFinished(AuthSession* auth_session,
                                  StatusCallback on_done,
                                  CryptohomeStatus status);

  // Populates the user session key data from the auth session key data.
  void SetKeyDataForUserSession(AuthSession* auth_session,
                                bool override_existing_data);

  // =============== WebAuthn Related Helpers ===============

  bool PrepareWebAuthnSecret(const std::string& account_id,
                             const VaultKeyset& vk);

  // =============== USS Experiment Related Methods ===============

  // Called on Mount thread. This creates a dbus proxy for flimflam::Manager
  // and connects to signals. It fetches the experiment config from gstatic when
  // network is ready.
  void CreateUssExperimentConfigFetcher();

  // =============== PinWeaver Related Methods ===============

  // Called on Mount thread. Pairing secret (Pk) is established once per
  // powerwash cycle after the device first boots. An ECDH protocol is used
  // between biometrics AuthStacks and GSC to establish Pk. This function blocks
  // future Pk establishment attempts made by biometrics AuthStacks, as we
  // considered device state becoming more vulnerable after entering user
  // session. For example, an attacker can try to send EC commands to FPMCU and
  // send vendor commands to GSC to complete a person-in-the-middle attack on
  // the ECDH protocol used for Pk establishment.
  void BlockPkEstablishment();

  // =============== Threading Related Variables ===============

  // The task runner that belongs to the thread that created this UserDataAuth
  // object. Currently, this is required to be the same as the dbus thread's
  // task runner.
  scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner_;

  // The thread ID of the thread that created this UserDataAuth object.
  // Currently, this is required to be th esame as the dbus thread's task
  // runner.
  base::PlatformThreadId origin_thread_id_;

  // The thread for performing long running, or mount related operations
  std::unique_ptr<MountThread> mount_thread_;

  // The task runner that belongs to the mount thread.
  scoped_refptr<base::SingleThreadTaskRunner> mount_task_runner_;

  // =============== Basic Utilities Related Variables ===============
  // The system salt that is used for obfuscating the username
  brillo::SecureBlob system_salt_;

  // The default hwsec factory object.
  std::unique_ptr<hwsec::Factory> default_hwsec_factory_;

  // The object to generate the other frontends.
  hwsec::Factory* hwsec_factory_;

  // The default object for accessing the HWSec related functions.
  std::unique_ptr<hwsec::CryptohomeFrontend> default_hwsec_;

  // The object for accessing the HWSec related functions.
  hwsec::CryptohomeFrontend* hwsec_;

  // The default object for accessing the pinweaver related functions.
  std::unique_ptr<hwsec::PinWeaverFrontend> default_pinweaver_;

  // The object for accessing the pinweaver related functions.
  hwsec::PinWeaverFrontend* pinweaver_;

  // The default object for accessing the recovery crypto related functions.
  std::unique_ptr<hwsec::RecoveryCryptoFrontend> default_recovery_crypto_;

  // The object for accessing the recovery crypto related functions.
  hwsec::RecoveryCryptoFrontend* recovery_crypto_;

  // The default cryptohome key loader object
  std::unique_ptr<CryptohomeKeysManager> default_cryptohome_keys_manager_;

  // The cryptohome key loader object
  CryptohomeKeysManager* cryptohome_keys_manager_;

  tpm_manager::TpmManagerUtility* tpm_manager_util_;

  // The default platform object for accessing platform related functionalities
  std::unique_ptr<cryptohome::Platform> default_platform_;

  // The actual platform object used by this class, usually set to
  // default_platform_, but can be overridden for testing
  cryptohome::Platform* platform_;

  // The default crypto object for performing cryptographic operations
  std::unique_ptr<cryptohome::Crypto> default_crypto_;

  // The actual crypto object used by this class, usually set to
  // default_crypto_, but can be overridden for testing
  cryptohome::Crypto* crypto_;

  // The default token manager client for accessing chapsd's PKCS#11 interface
  std::unique_ptr<chaps::TokenManagerClient> default_chaps_client_;

  // The actual token manager client used by this class, usually set to
  // default_chaps_client_, but can be overridden for testing.
  chaps::TokenManagerClient* chaps_client_;

  // A dbus connection, this is used by any code in this class that needs access
  // to the system DBus and accesses it on the origin thread.
  scoped_refptr<::dbus::Bus> bus_;

  // A dbus connection, this is used by any code in this class that needs access
  // to the system DBus and accesses it on the mount thread. Such as when
  // creating an instance of KeyChallengeService.
  scoped_refptr<::dbus::Bus> mount_thread_bus_;

  // The default PKCS#11 init object that is used to supply some PKCS#11 related
  // information.
  std::unique_ptr<Pkcs11Init> default_pkcs11_init_;

  // The actual PKCS#11 init object that is used by this class, but can be
  // overridden for testing.
  Pkcs11Init* pkcs11_init_;

  // The default factory for Pkcs11Token objects.
  std::unique_ptr<Pkcs11TokenFactory> default_pkcs11_token_factory_;

  // The actual factory for Pkcs11TokenObjects.
  Pkcs11TokenFactory* pkcs11_token_factory_;

  // The default Firmware Management Parameters object for accessing any
  // Firmware Management Parameters related functionalities.
  std::unique_ptr<FirmwareManagementParameters>
      default_firmware_management_params_;

  // The actual Firmware Management Parameters object that is used by this
  // class, but can be overridden for testing.
  FirmwareManagementParameters* firmware_management_parameters_;

  // The default Fingerprint Manager object for fingerprint authentication.
  std::unique_ptr<FingerprintManager> default_fingerprint_manager_;

  // The actual Fingerprint Manager object that is used by this class, but
  // can be overridden for testing.
  FingerprintManager* fingerprint_manager_;

  // This is set to true iff OwnershipCallback has run.
  bool ownership_callback_has_run_;

  // =============== Install Attributes Related Variables ===============

  // The default install attributes object, for accessing install attributes
  // related functionality.
  std::unique_ptr<cryptohome::InstallAttributes> default_install_attrs_;

  // The actual install attributes object used by this class, usually set to
  // |default_install_attrs_|, but can be overridden for testing. This object
  // should only be accessed on the origin thread.
  cryptohome::InstallAttributes* install_attrs_;

  // Whether this device is an enterprise owned device. Write access should only
  // happen on mount thread.
  bool enterprise_owned_;

  // =============== Mount Related Variables ===============

  // This holds a timestamp for each user that is the time that the user was
  // active.
  std::unique_ptr<UserOldestActivityTimestampManager>
      default_user_activity_timestamp_manager_;
  // Usually points to |default_user_activity_timestamp_manager_|, but can be
  // overridden for testing.
  UserOldestActivityTimestampManager* user_activity_timestamp_manager_ =
      nullptr;

  std::unique_ptr<CryptohomeVaultFactory> default_vault_factory_;
  // Usually points to |default_vault_factory_|, but can be overridden for
  // testing.
  CryptohomeVaultFactory* vault_factory_ = nullptr;

  // The homedirs_ object in normal operation
  std::unique_ptr<HomeDirs> default_homedirs_;

  // This holds the object that records informations about the homedirs.
  // This is usually set to default_homedirs_, but can be overridden for
  // testing.
  // This is to be accessed from the mount thread only because there's no
  // guarantee on thread safety of the HomeDirs object.
  HomeDirs* homedirs_;

  // The keyset_management_ object in normal operation.
  std::unique_ptr<KeysetManagement> default_keyset_management_;
  // This holds the object that records information about the
  // keyset_management. This is usually set to default_keyset_management_, but
  // can be overridden for testing. This is to be accessed from the mount thread
  // only because there's no guarantee on thread safety of the HomeDirs object.
  KeysetManagement* keyset_management_;

  // Default challenge credential helper utility object. This object is required
  // for doing a challenge response style login, and is only lazily created when
  // mounting a mount that requires challenge response login type is performed.
  std::unique_ptr<ChallengeCredentialsHelper>
      default_challenge_credentials_helper_;

  // Actual challenge credential helper utility object used by this class.
  // Usually set to |default_challenge_credentials_helper_|, but can be
  // overridden for testing.
  ChallengeCredentialsHelper* challenge_credentials_helper_ = nullptr;

  // The repeating callback to send FingerprintScanResult signal.
  base::RepeatingCallback<void(user_data_auth::FingerprintScanResult)>
      fingerprint_scan_result_callback_;

  // The object used to instantiate AuthBlocks.
  std::unique_ptr<AuthBlockUtility> default_auth_block_utility_;
  // This holds the object that records information about the
  // auth_block_utility. This is usually set to default_auth_block_utility_, but
  // can be overridden for testing. This is to be accessed from the mount thread
  // only because there's no guarantee on thread safety of the HomeDirs object.
  AuthBlockUtility* auth_block_utility_;

  // Manager of auth factor files.
  std::unique_ptr<AuthFactorManager> default_auth_factor_manager_;
  // Usually set to |default_auth_factor_manager_|, but can be overridden for
  // tests.
  AuthFactorManager* auth_factor_manager_ = nullptr;

  // User secret stash storage helper.
  std::unique_ptr<UserSecretStashStorage> default_user_secret_stash_storage_;
  // Usually set to |default_user_secret_stash_storage_|, but can be overridden
  // for tests.
  UserSecretStashStorage* user_secret_stash_storage_ = nullptr;

  // Records the UserSession objects associated with each username.
  // This and its content should only be accessed from the mount thread.
  UserSessionMap default_sessions_;
  // Usually points to |default_sessions_|, but can be overridden for tests.
  UserSessionMap* sessions_ = &default_sessions_;

  // Manager for auth session objects.
  std::unique_ptr<AuthSessionManager> default_auth_session_manager_;
  // Usually set to default_auth_session_manager_, but can be overridden for
  // tests.
  AuthSessionManager* auth_session_manager_;

  // The low_disk_space_handler_ object in normal operation
  std::unique_ptr<LowDiskSpaceHandler> default_low_disk_space_handler_;

  // This holds the object that checks for low disk space and performs disk
  // cleanup.
  // This is to be accessed from the mount thread only because there's no
  // guarantee on thread safety of the HomeDirs object.
  LowDiskSpaceHandler* low_disk_space_handler_;

  // TODO(dlunev): This three variables are a hack to pass cleanup parameters
  // from main to the actual object. The reason it is done like this is that
  // the object is created in UserDataAuth::Initialize, which is called from the
  // daemonization function, but they are attempted to be set from the main,
  // before the daemonization. Once service.cc is gone, we shall refactor the
  // whole initialization process of UserDataAuth to avoid such hacks.
  uint64_t disk_cleanup_threshold_;
  uint64_t disk_cleanup_aggressive_threshold_;
  uint64_t disk_cleanup_critical_threshold_;
  uint64_t disk_cleanup_target_free_space_;

  // Factory for creating |Mount| objects.
  std::unique_ptr<MountFactory> default_mount_factory_;
  // This usually points to |default_mount_factory_|, but can be overridden in
  // tests.
  MountFactory* mount_factory_ = nullptr;

  // The default user session factory instance that can be used by this class to
  // create UserSession object.
  std::unique_ptr<UserSessionFactory> default_user_session_factory_;

  // The user session factory instance that can be overridden for tests.
  UserSessionFactory* user_session_factory_;

  // This holds the salt that is used to derive the passkey for public mounts.
  brillo::SecureBlob public_mount_salt_;

  // Default factory of key challenge services. This object is required for
  // doing a challenge response style login.
  KeyChallengeServiceFactoryImpl default_key_challenge_service_factory_;

  // Actual factory of key challenge services that is used by this class.
  // Usually set to |default_key_challenge_service_factory_|, but can be
  // overridden for testing.
  KeyChallengeServiceFactory* key_challenge_service_factory_ =
      &default_key_challenge_service_factory_;

  // Guest user's username.
  std::string guest_user_;

  // Force the use of eCryptfs. If eCryptfs is not used, then dircrypto (the
  // ext4 directory encryption) is used.
  bool force_ecryptfs_;

  // Force v2 version for fscrypt interface.
  bool fscrypt_v2_;

  // Enable creation of LVM volumes for applications.
  bool enable_application_containers_;

  // Whether we are using legacy mount. See Mount::MountLegacyHome()'s comment
  // for more information.
  bool legacy_mount_;

  // Whether Downloads/ should be bind mounted.
  bool bind_mount_downloads_;

  // The default ARC Disk Quota object. This is used to provide Quota related
  // information function for ARC.
  std::unique_ptr<ArcDiskQuota> default_arc_disk_quota_;

  // The actual ARC Disk Quota object used by this class. Usually set to
  // default_arc_disk_quota_, but can be overridden for testing.
  ArcDiskQuota* arc_disk_quota_;

  // A counter to count the number of parallel tasks on mount thread.
  // Recorded when a requests comes in. Counts of 1 will not reported.
  std::atomic<int> parallel_task_count_ = 0;

  // The default USS experiment config fetcher object. This is used to fetch the
  // USS experiment config when network is first connected.
  std::unique_ptr<UssExperimentConfigFetcher>
      default_uss_experiment_config_fetcher_;

  // The actual USS experiment config fetcher object. Usually set to
  // default_uss_experiment_config_fetcher_, but can be overridden for testing.
  UssExperimentConfigFetcher* uss_experiment_config_fetcher_;

  // Flag to cache the status of whether Pk establishment is blocked
  // successfully, so we don't have to do this multiple times.
  bool pk_establishment_blocked_;

  friend class AuthSessionTestWithKeysetManagement;
  FRIEND_TEST(AuthSessionTestWithKeysetManagement,
              StartAuthSessionWithoutKeyData);

  friend class UserDataAuthTestTasked;
  FRIEND_TEST(UserDataAuthTest, Unmount_AllDespiteFailures);
  FRIEND_TEST(UserDataAuthTest, InitializePkcs11Unmounted);

  friend class UserDataAuthExTest;
  FRIEND_TEST(UserDataAuthExTest, ExtendAuthSession);
  FRIEND_TEST(UserDataAuthExTest, ExtendUnAuthenticatedAuthSessionFail);
  FRIEND_TEST(UserDataAuthExTest, CheckTimeoutTimerSetAfterAuthentication);
  FRIEND_TEST(UserDataAuthExTest, InvalidateAuthSession);
  FRIEND_TEST(UserDataAuthExTest, MountUnauthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest, RemoveValidityWithAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSessionUnusableClobber);
  FRIEND_TEST(UserDataAuthExTest,
              StartMigrateToDircryptoWithAuthenticatedAuthSession);
  FRIEND_TEST(UserDataAuthExTest,
              StartMigrateToDircryptoWithUnAuthenticatedAuthSession);

  friend class AuthSessionInterfaceTestBase;
  friend class AuthSessionInterfaceTest;
  friend class AuthSessionInterfaceMockAuthTest;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USERDATAAUTH_H_
