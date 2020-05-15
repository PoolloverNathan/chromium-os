// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <map>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <sane/saneopts.h>

namespace lorgnette {

// static
std::unique_ptr<SaneClientImpl> SaneClientImpl::Create() {
  SANE_Status status = sane_init(nullptr, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "Unable to initialize SANE";
    return nullptr;
  }

  // Cannot use make_unique() with a private constructor.
  return std::unique_ptr<SaneClientImpl>(new SaneClientImpl());
}

SaneClientImpl::~SaneClientImpl() {
  sane_exit();
}

bool SaneClientImpl::ListDevices(brillo::ErrorPtr* error,
                                 Manager::ScannerInfo* info_out) {
  base::AutoLock auto_lock(lock_);
  const SANE_Device** device_list;
  SANE_Status status = sane_get_devices(&device_list, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get device list from SANE");
    return false;
  }

  return DeviceListToScannerInfo(device_list, info_out);
}

// static
bool SaneClientImpl::DeviceListToScannerInfo(const SANE_Device** device_list,
                                             Manager::ScannerInfo* info_out) {
  if (!device_list || !info_out) {
    return false;
  }

  Manager::ScannerInfo scanners;
  for (int i = 0; device_list[i]; i++) {
    const SANE_Device* dev = device_list[i];
    if (!dev->name || strcmp(dev->name, "") == 0)
      continue;

    if (scanners.count(dev->name) != 0)
      return false;

    std::map<std::string, std::string> scanner_info;
    scanner_info[kScannerPropertyManufacturer] = dev->vendor ? dev->vendor : "";
    scanner_info[kScannerPropertyModel] = dev->model ? dev->model : "";
    scanner_info[kScannerPropertyType] = dev->type ? dev->type : "";
    scanners[dev->name] = scanner_info;
  }
  *info_out = scanners;
  return true;
}

SaneClientImpl::SaneClientImpl()
    : open_devices_(std::make_shared<DeviceSet>()) {}

std::unique_ptr<SaneDevice> SaneClientImpl::ConnectToDevice(
    brillo::ErrorPtr* error, const std::string& device_name) {
  base::AutoLock auto_lock(lock_);
  SANE_Handle handle;
  SANE_Status status = sane_open(device_name.c_str(), &handle);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to open device '%s': %s",
                               device_name.c_str(), sane_strstatus(status));
    return nullptr;
  }

  {
    base::AutoLock auto_lock(open_devices_->first);
    if (open_devices_->second.count(device_name) != 0) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Device '%s' is currently in-use", device_name.c_str());
      return nullptr;
    }
    open_devices_->second.insert(device_name);
  }

  // Cannot use make_unique() with a private constructor.
  auto device = std::unique_ptr<SaneDeviceImpl>(
      new SaneDeviceImpl(handle, device_name, open_devices_));
  device->LoadOptions(error);
  return device;
}

SaneDeviceImpl::~SaneDeviceImpl() {
  if (handle_)
    sane_close(handle_);
  base::AutoLock auto_lock(open_devices_->first);
  open_devices_->second.erase(name_);
}

bool SaneDeviceImpl::GetValidOptionValues(brillo::ErrorPtr* error,
                                          ValidOptionValues* values_out) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scanner connected");
    return false;
  }

  if (!values_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'values_out' pointer cannot be null");
    return false;
  }

  ValidOptionValues values;
  if (!GetValidIntOptionValues(error, kResolution, &values.resolutions)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Failed to get valid values for resolution setting");
    return false;
  }

  if (!GetValidStringOptionValues(error, kSource, &values.sources)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Failed to get valid values for sources setting");
    return false;
  }

  if (!GetValidStringOptionValues(error, kScanMode, &values.color_modes)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Failed to get valid values for scan modes setting");
    return false;
  }

  *values_out = values;
  return true;
}

bool SaneDeviceImpl::SetScanResolution(brillo::ErrorPtr* error,
                                       int resolution) {
  if (options_.count(kResolution) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No resolution option found.");
    return false;
  }

  SaneOption option = options_[kResolution];
  option.SetInt(resolution);

  bool should_reload = false;
  SANE_Status status = SetOption(&option, &should_reload);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to set resolution to %d: %s", resolution,
                               sane_strstatus(status));
    return false;
  }
  if (should_reload)
    LoadOptions(error);

  return true;
}

bool SaneDeviceImpl::SetScanMode(brillo::ErrorPtr* error,
                                 const std::string& scan_mode) {
  if (options_.count(kScanMode) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scan mode option found.");
    return false;
  }

  SaneOption option = options_[kScanMode];
  option.SetString(scan_mode);

  bool should_reload = false;
  SANE_Status status = SetOption(&option, &should_reload);
  if (should_reload)
    LoadOptions(error);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to set scan mode to %s: %s",
                               scan_mode.c_str(), sane_strstatus(status));
    return false;
  }
  return true;
}

bool SaneDeviceImpl::StartScan(brillo::ErrorPtr* error) {
  if (scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Scan is already in progress");
    return false;
  }

  SANE_Status status = sane_start(handle_);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError, "Failed to start scan: %s",
                               sane_strstatus(status));
    return false;
  }

  scan_running_ = true;
  return true;
}

bool SaneDeviceImpl::GetScanParameters(brillo::ErrorPtr* error,
                                       ScanParameters* parameters) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scanner connected");
    return false;
  }

  if (!parameters) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'parameters' pointer cannot be null");
    return false;
  }

  SANE_Parameters params;
  SANE_Status status = sane_get_parameters(handle_, &params);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Failed to read scan parameters: %s", sane_strstatus(status));
    return false;
  }

  switch (params.format) {
    case SANE_FRAME_GRAY:
      parameters->format = kGrayscale;
      break;
    case SANE_FRAME_RGB:
      parameters->format = kRGB;
      break;
    default:
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           kManagerServiceError,
                           "Unsupported scan frame format");
      return false;
  }

  parameters->bytes_per_line = params.bytes_per_line;
  parameters->pixels_per_line = params.pixels_per_line;
  parameters->lines = params.lines;
  parameters->depth = params.depth;
  return true;
}

bool SaneDeviceImpl::ReadScanData(brillo::ErrorPtr* error,
                                  uint8_t* buf,
                                  size_t count,
                                  size_t* read_out) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scanner connected");
    return false;
  }

  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scan in progress");
    return false;
  }

  if (!buf || !read_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'buf' and 'read' pointers cannot be null");
    return false;
  }
  SANE_Int read = 0;
  SANE_Status status = sane_read(handle_, buf, count, &read);
  switch (status) {
    case SANE_STATUS_GOOD:
      *read_out = read;
      return true;
    case SANE_STATUS_EOF:
      *read_out = 0;
      scan_running_ = false;
      // sane_cancel() must always be called once a scan has completed.
      sane_cancel(handle_);
      return true;
    default:
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "sane_read() failed: %s", sane_strstatus(status));
      return false;
  }
}

bool SaneDeviceImpl::SaneOption::SetInt(int i) {
  switch (type) {
    case SANE_TYPE_INT:
      value.i = i;
      return true;
    case SANE_TYPE_FIXED:
      value.f = SANE_FIX(static_cast<double>(i));
      return true;
    default:
      return false;
  }
}

bool SaneDeviceImpl::SaneOption::SetString(const std::string& s) {
  if (type != SANE_TYPE_STRING) {
    return false;
  }

  if (value.s)
    free(value.s);
  value.s = strdup(s.c_str());
  if (!value.s)
    return false;

  return true;
}

SaneDeviceImpl::SaneOption::~SaneOption() {
  if (type == SANE_TYPE_STRING)
    free(value.s);
}

SaneDeviceImpl::SaneDeviceImpl(SANE_Handle handle,
                               const std::string& name,
                               std::shared_ptr<DeviceSet> open_devices)
    : handle_(handle),
      name_(name),
      open_devices_(open_devices),
      scan_running_(false) {}

bool SaneDeviceImpl::LoadOptions(brillo::ErrorPtr* error) {
  // First we get option descriptor 0, which contains the total count of
  // options. We don't strictly need the descriptor, but it's "Good form" to
  // do so according to 'scanimage'.
  const SANE_Option_Descriptor* desc = sane_get_option_descriptor(handle_, 0);
  if (!desc) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get option count for device");
    return false;
  }

  SANE_Int num_options = 0;
  SANE_Status status = sane_control_option(handle_, 0, SANE_ACTION_GET_VALUE,
                                           &num_options, nullptr);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get option count for device");
    return false;
  }

  options_.clear();
  // Start at 1, since we've already checked option 0 above.
  for (int i = 1; i < num_options; i++) {
    const SANE_Option_Descriptor* opt = sane_get_option_descriptor(handle_, i);
    if (!opt) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Unable to get option %d for device", i);
      return false;
    }

    if ((opt->type == SANE_TYPE_INT || opt->type == SANE_TYPE_FIXED) &&
        opt->size == sizeof(SANE_Int) && opt->unit == SANE_UNIT_DPI &&
        strcmp(opt->name, SANE_NAME_SCAN_RESOLUTION) == 0) {
      options_[kResolution].index = i;
      options_[kResolution].type = opt->type;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_MODE) == 0) {
      options_[kScanMode].index = i;
      options_[kScanMode].type = opt->type;
      options_[kScanMode].value.s = NULL;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_SOURCE) == 0) {
      options_[kSource].index = i;
      options_[kSource].type = opt->type;
      options_[kSource].value.s = NULL;
    }
  }

  return true;
}

SANE_Status SaneDeviceImpl::SetOption(SaneOption* option, bool* should_reload) {
  void* value;
  switch (option->type) {
    case SANE_TYPE_INT:
      value = &option->value.i;
      break;
    case SANE_TYPE_FIXED:
      value = &option->value.f;
      break;
    case SANE_TYPE_STRING:
      // Do not use '&' here, since SANE_String is already a pointer type.
      value = option->value.s;
      break;
    default:
      return SANE_STATUS_UNSUPPORTED;
  }

  SANE_Int result_flags;
  SANE_Status status = sane_control_option(
      handle_, option->index, SANE_ACTION_SET_VALUE, value, &result_flags);
  if (status != SANE_STATUS_GOOD) {
    return status;
  }

  if (result_flags & SANE_INFO_RELOAD_OPTIONS) {
    *should_reload = true;
  }

  return status;
}

bool SaneDeviceImpl::GetValidStringOptionValues(
    brillo::ErrorPtr* error,
    ScanOption option,
    std::vector<std::string>* values_out) {
  if (!values_out)
    return false;

  if (options_.count(option) == 0) {
    return false;
  }

  int index = options_[option].index;
  const SANE_Option_Descriptor* opt =
      sane_get_option_descriptor(handle_, index);
  if (!opt) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Unable to get option descriptor (%d) for device", index);
    return false;
  }

  if (opt->constraint_type != SANE_CONSTRAINT_STRING_LIST) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Invalid option constraint type %d", opt->constraint_type);
    return false;
  }

  std::vector<std::string> values;
  for (const SANE_String_Const* s = opt->constraint.string_list; s; s++) {
    values.push_back(*s);
  }

  *values_out = values;
  return true;
}

bool SaneDeviceImpl::GetValidIntOptionValues(
    brillo::ErrorPtr* error,
    ScanOption option,
    std::vector<uint32_t>* values_out) {
  if (!values_out)
    return false;

  if (options_.count(option) == 0) {
    return false;
  }

  int index = options_[option].index;
  const SANE_Option_Descriptor* opt =
      sane_get_option_descriptor(handle_, index);
  if (!opt) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Unable to get option descriptor (%d) for device", index);
    return false;
  }

  if (opt->constraint_type != SANE_CONSTRAINT_WORD_LIST) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Invalid option constraint type %d", opt->constraint_type);
    return false;
  }

  std::vector<uint32_t> values;
  int num_values = opt->constraint.word_list[0];
  for (int i = 1; i <= num_values; i++) {
    SANE_Word w = opt->constraint.word_list[i];
    int value = opt->type == SANE_TYPE_FIXED ? SANE_UNFIX(w) : w;
    values.push_back(value);
  }

  *values_out = values;
  return true;
}

}  // namespace lorgnette
