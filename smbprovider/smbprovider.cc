// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/smbprovider.h"

#include <map>
#include <utility>

#include <base/files/file_path.h>
#include <base/memory/ptr_util.h>

#include "smbprovider/constants.h"
#include "smbprovider/mount_manager.h"
#include "smbprovider/proto.h"
#include "smbprovider/proto_bindings/directory_entry.pb.h"
#include "smbprovider/samba_interface_impl.h"
#include "smbprovider/smbprovider_helper.h"

namespace smbprovider {

SmbProvider::SmbProvider(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    std::unique_ptr<SambaInterface> samba_interface,
    std::unique_ptr<MountManager> mount_manager,
    size_t buffer_size)
    : org::chromium::SmbProviderAdaptor(this),
      samba_interface_(std::move(samba_interface)),
      dbus_object_(std::move(dbus_object)),
      mount_manager_(std::move(mount_manager)),
      dir_buf_(buffer_size) {}

SmbProvider::SmbProvider(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    std::unique_ptr<SambaInterface> samba_interface,
    std::unique_ptr<MountManager> mount_manager)
    : SmbProvider(std::move(dbus_object),
                  std::move(samba_interface),
                  std::move(mount_manager),
                  kBufferSize) {}

void SmbProvider::RegisterAsync(
    const AsyncEventSequencer::CompletionAction& completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

void SmbProvider::Mount(const ProtoBlob& options_blob,
                        int32_t* error_code,
                        int32_t* mount_id) {
  DCHECK(error_code);
  DCHECK(mount_id);
  *mount_id = -1;

  MountOptionsProto options;
  bool can_mount = ParseOptionsProto(options_blob, &options, error_code) &&
                   CanMountPath(options.path(), error_code);

  if (!can_mount) {
    // ParseOptionsProto() or CanMountPath() already set |error_code|.
    return;
  }

  *mount_id = mount_manager_->AddMount(options.path());
  *error_code = static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::Unmount(const ProtoBlob& options_blob) {
  int32_t error_code;
  UnmountOptionsProto options;
  if (!ParseOptionsProto(options_blob, &options, &error_code) ||
      !RemoveMount(options.mount_id(), &error_code)) {
    return error_code;
  }

  return ERROR_OK;
}

void SmbProvider::ReadDirectory(const ProtoBlob& options_blob,
                                int32_t* error_code,
                                ProtoBlob* out_entries) {
  DCHECK(error_code);
  DCHECK(out_entries);
  out_entries->clear();

  std::string full_path;
  ReadDirectoryOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    return;
  }

  int32_t dir_id = -1;
  int32_t open_dir_error = samba_interface_->OpenDirectory(full_path, &dir_id);
  if (open_dir_error != 0) {
    LogAndSetError(options, GetErrorFromErrno(open_dir_error), error_code);
    return;
  }
  DirectoryEntryListProto directory_entries;
  int32_t get_dir_error = GetDirectoryEntries(dir_id, &directory_entries);
  if (get_dir_error != 0) {
    LogAndSetError(options, GetErrorFromErrno(get_dir_error), error_code);
    CloseDirectory(dir_id);
    return;
  }
  *error_code = static_cast<int32_t>(
      SerializeProtoToBlob(directory_entries, out_entries));
  CloseDirectory(dir_id);
}

void SmbProvider::GetMetadataEntry(const ProtoBlob& options_blob,
                                   int32_t* error_code,
                                   ProtoBlob* out_entry) {
  DCHECK(error_code);
  DCHECK(out_entry);
  out_entry->clear();

  std::string full_path;
  GetMetadataEntryOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    return;
  }

  struct stat stat_info;
  int32_t get_status_error =
      samba_interface_->GetEntryStatus(full_path.c_str(), &stat_info);
  if (get_status_error != 0) {
    LogAndSetError(options, GetErrorFromErrno(get_status_error), error_code);
    return;
  }
  *error_code = GetDirectoryEntryProtoFromStat(full_path, stat_info, out_entry);
}

void SmbProvider::OpenFile(const ProtoBlob& options_blob,
                           int32_t* error_code,
                           int32_t* file_id) {
  DCHECK(error_code);
  DCHECK(file_id);

  std::string full_path;
  OpenFileOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, error_code)) {
    return;
  }

  if (!OpenFile(options, full_path, error_code, file_id)) {
    *file_id = -1;
    return;
  }

  *error_code = static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::CloseFile(const ProtoBlob& options_blob) {
  int32_t error_code;
  CloseFileOptionsProto options;
  if (!ParseOptionsProto(options_blob, &options, &error_code)) {
    return error_code;
  }

  if (!CloseFile(options, options.file_id(), &error_code)) {
    return error_code;
  }

  return static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::DeleteEntry(const ProtoBlob& options_blob) {
  int32_t error_code;
  DeleteEntryOptionsProto options;
  std::string full_path;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, &error_code)) {
    return error_code;
  }

  if (options.recursive()) {
    NOTIMPLEMENTED();
  }

  bool is_directory;
  int32_t get_type_result;
  if (!GetEntryType(full_path, &get_type_result, &is_directory)) {
    LogAndSetError(options, GetErrorFromErrno(get_type_result), &error_code);
    return error_code;
  }

  int32_t result;
  if (is_directory) {
    result = samba_interface_->RemoveDirectory(full_path.c_str());
  } else {
    result = samba_interface_->Unlink(full_path.c_str());
  }

  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), &error_code);
    return error_code;
  }

  return static_cast<int32_t>(ERROR_OK);
}

void SmbProvider::ReadFile(const ProtoBlob& options_blob,
                           int32_t* error_code,
                           dbus::FileDescriptor* temp_fd) {
  DCHECK(error_code);
  DCHECK(temp_fd);

  // TODO(allenvic): Investigate having a single shared buffer in the class.
  std::vector<uint8_t> buffer;
  ReadFileOptionsProto options;

  ParseOptionsProto(options_blob, &options, error_code) &&
      Seek(options, error_code) &&
      ReadFileIntoBuffer(options, error_code, &buffer) &&
      WriteTempFile(options, buffer, error_code, temp_fd);
}

int32_t SmbProvider::CreateFile(const ProtoBlob& options_blob) {
  int32_t error_code;
  std::string full_path;
  CreateFileOptionsProto options;
  if (!ParseOptionsAndPath(options_blob, &options, &full_path, &error_code)) {
    return error_code;
  }

  int32_t file_id;
  // CreateFile() gives us back an open file descriptor to the newly created
  // file.
  int32_t create_result = samba_interface_->CreateFile(full_path, &file_id);
  if (create_result != 0) {
    LogAndSetError(options, GetErrorFromErrno(create_result), &error_code);
    return error_code;
  }

  // Close the file handle from CreateFile().
  if (!CloseFile(options, file_id, &error_code)) {
    // Attempt to delete the file since file will not be usable.
    int32_t unlink_result = samba_interface_->Unlink(full_path);
    if (unlink_result != 0) {
      // Log the unlink error but return the original error.
      LOG(ERROR) << "Error unlinking after error closing file: "
                 << GetErrorFromErrno(unlink_result);
    }
    return error_code;
  }

  return static_cast<int32_t>(ERROR_OK);
}

int32_t SmbProvider::Truncate(const ProtoBlob& options_blob) {
  NOTIMPLEMENTED();
  return 0;
}

// This is a helper method that has a similar return structure as
// samba_interface_ methods, where it will return errno as an error in case of
// failure.
int32_t SmbProvider::GetDirectoryEntries(int32_t dir_id,
                                         DirectoryEntryListProto* entries) {
  std::vector<DirectoryEntry> entries_vector;
  int32_t result = GetDirectoryEntriesVector(dir_id, &entries_vector);
  if (result != 0) {
    return result;
  }
  SerializeDirEntryVectorToProto(entries_vector, entries);
  return 0;
}

int32_t SmbProvider::GetDirectoryEntriesVector(
    int32_t dir_id, std::vector<DirectoryEntry>* entries) {
  DCHECK(entries);
  int32_t bytes_read = 0;
  do {
    int32_t result = samba_interface_->GetDirectoryEntries(
        dir_id, GetDirentFromBuffer(dir_buf_.data()), dir_buf_.size(),
        &bytes_read);
    if (result != 0) {
      // The result will be set to errno on failure.
      return result;
    }
    int32_t bytes_left = bytes_read;
    smbc_dirent* dirent = GetDirentFromBuffer(dir_buf_.data());
    while (bytes_left > 0) {
      AddEntryIfValid(*dirent, entries);
      DCHECK_GT(dirent->dirlen, 0);
      DCHECK_GE(bytes_left, dirent->dirlen);
      bytes_left -= dirent->dirlen;
      dirent = AdvanceDirEnt(dirent);
      DCHECK(dirent);
    }
    DCHECK_EQ(bytes_left, 0);
  } while (bytes_read > 0);
  return 0;
}

// TODO(zentaro): When the proto's with missing mount_id are landed, this can
// take a generic *Options proto and derive the operation name and entry path
// itself.
bool SmbProvider::GetFullPath(const char* operation_name,
                              int32_t mount_id,
                              const std::string& entry_path,
                              std::string* full_path) const {
  if (!mount_manager_->GetFullPath(mount_id, entry_path, full_path)) {
    LOG(ERROR) << operation_name << " requested unknown mount_id " << mount_id;
    return false;
  }

  return true;
}

template <typename Proto>
bool SmbProvider::ParseOptionsAndPath(const ProtoBlob& blob,
                                      Proto* options,
                                      std::string* full_path,
                                      int32_t* error_code) {
  if (!ParseOptionsProto(blob, options, error_code)) {
    return false;
  }

  if (!GetFullPath(GetMethodName(*options), options->mount_id(),
                   GetEntryPath(*options), full_path)) {
    *error_code = static_cast<int32_t>(ERROR_NOT_FOUND);
    return false;
  }

  return true;
}

bool SmbProvider::GetEntryType(const std::string& full_path,
                               int32_t* error_code,
                               bool* is_directory) {
  struct stat stat_info;
  *error_code = samba_interface_->GetEntryStatus(full_path.c_str(), &stat_info);
  if (*error_code != 0) {
    return false;
  }

  if (IsDirectory(stat_info)) {
    *is_directory = true;
    return true;
  }
  if (IsFile(stat_info)) {
    *is_directory = false;
    return true;
  }
  *error_code = ENOENT;
  return false;
}

template <typename Proto>
bool SmbProvider::Seek(const Proto& options, int32_t* error_code) {
  int32_t result = samba_interface_->Seek(options.file_id(), options.offset());
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }
  return true;
}

bool SmbProvider::CanMountPath(const std::string& mount_root,
                               int32_t* error_code) {
  int32_t dir_id = -1;
  int32_t result = samba_interface_->OpenDirectory(mount_root, &dir_id);
  if (result != 0) {
    LogAndSetError(kMountMethod, -1, GetErrorFromErrno(result), error_code);
    return false;
  }

  CloseDirectory(dir_id);
  return true;
}

void SmbProvider::CloseDirectory(int32_t dir_id) {
  int32_t result = samba_interface_->CloseDirectory(dir_id);
  if (result != 0) {
    LOG(ERROR) << "Error closing directory " << dir_id;
  }
}

bool SmbProvider::RemoveMount(int32_t mount_id, int32_t* error_code) {
  bool removed = mount_manager_->RemoveMount(mount_id);
  if (!removed) {
    *error_code = static_cast<int32_t>(ERROR_NOT_FOUND);
  }

  return removed;
}

bool SmbProvider::ReadFileIntoBuffer(const ReadFileOptionsProto& options,
                                     int32_t* error_code,
                                     std::vector<uint8_t>* buffer) {
  DCHECK(buffer);
  DCHECK(error_code);

  buffer->resize(options.length());
  size_t bytes_read;
  int32_t result = samba_interface_->ReadFile(options.file_id(), buffer->data(),
                                              buffer->size(), &bytes_read);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error_code);
    return false;
  }

  DCHECK_GE(bytes_read, 0);
  DCHECK_LE(bytes_read, buffer->size());
  // Make sure buffer is only as big as bytes_read.
  buffer->resize(bytes_read);
  return true;
}

bool SmbProvider::WriteTempFile(const ReadFileOptionsProto& options,
                                const std::vector<uint8_t>& buffer,
                                int32_t* error_code,
                                dbus::FileDescriptor* temp_fd) {
  base::ScopedFD scoped_fd = temp_file_manager_.CreateTempFile(buffer);
  if (!scoped_fd.is_valid()) {
    LogAndSetError(options, ERROR_IO, error_code);
    return false;
  }

  GetValidDBusFD(&scoped_fd, temp_fd);
  *error_code = static_cast<int32_t>(ERROR_OK);
  return true;
}

template <typename Proto>
bool SmbProvider::OpenFile(const Proto& options,
                           const std::string& full_path,
                           int32_t* error,
                           int32_t* file_id) {
  int32_t result = samba_interface_->OpenFile(
      full_path, GetOpenFilePermissions(options), file_id);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error);
    return false;
  }
  return true;
}

template <typename Proto>
bool SmbProvider::CloseFile(const Proto& options,
                            int32_t file_id,
                            int32_t* error) {
  int32_t result = samba_interface_->CloseFile(file_id);
  if (result != 0) {
    LogAndSetError(options, GetErrorFromErrno(result), error);
    return false;
  }
  return true;
}

}  // namespace smbprovider
