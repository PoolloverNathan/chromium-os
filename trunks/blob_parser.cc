// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/blob_parser.h"

#include <base/check.h>
#include <base/logging.h>
#include <base/stl_util.h>

#include "trunks/error_codes.h"

namespace trunks {

bool BlobParser::SerializeKeyBlob(const TPM2B_PUBLIC& public_info,
                                  const TPM2B_PRIVATE& private_info,
                                  std::string* key_blob) {
  CHECK(key_blob) << "KeyBlob not defined.";
  key_blob->clear();
  if ((public_info.size == 0) && (private_info.size == 0)) {
    return true;
  }
  TPM_RC result = Serialize_TPM2B_PUBLIC(public_info, key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public info: " << GetErrorString(result);
    return false;
  }
  result = Serialize_TPM2B_PRIVATE(private_info, key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing private info: " << GetErrorString(result);
    return false;
  }
  return true;
}

bool BlobParser::ParseKeyBlob(const std::string& key_blob,
                              TPM2B_PUBLIC* public_info,
                              TPM2B_PRIVATE* private_info) {
  CHECK(public_info) << "Public info not defined.";
  CHECK(private_info) << "Private info not defined.";
  if (key_blob.empty()) {
    public_info->size = 0;
    private_info->size = 0;
    return true;
  }
  std::string mutable_key_blob = key_blob;
  TPM_RC result = Parse_TPM2B_PUBLIC(&mutable_key_blob, public_info, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing public info: " << GetErrorString(result);
    return false;
  }
  if (!public_info->size) {
    LOG(ERROR) << "Error parsing public info: Empty data";
    return false;
  }
  result = Parse_TPM2B_PRIVATE(&mutable_key_blob, private_info, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing private info: " << GetErrorString(result);
    return false;
  }
  if (!mutable_key_blob.empty()) {
    LOG(ERROR) << "Extra data at the end of the key blob.";
    return false;
  }
  return true;
}

bool BlobParser::SerializeCreationBlob(const TPM2B_CREATION_DATA& creation_data,
                                       const TPM2B_DIGEST& creation_hash,
                                       const TPMT_TK_CREATION& creation_ticket,
                                       std::string* creation_blob) {
  CHECK(creation_blob) << "CreationBlob not defined.";
  creation_blob->clear();
  TPM_RC result = Serialize_TPM2B_CREATION_DATA(creation_data, creation_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing creation_data: " << GetErrorString(result);
    return false;
  }
  result = Serialize_TPM2B_DIGEST(creation_hash, creation_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing creation_hash: " << GetErrorString(result);
    return false;
  }
  result = Serialize_TPMT_TK_CREATION(creation_ticket, creation_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing creation_ticket: "
               << GetErrorString(result);
    return false;
  }
  return true;
}

bool BlobParser::ParseCreationBlob(const std::string& creation_blob,
                                   TPM2B_CREATION_DATA* creation_data,
                                   TPM2B_DIGEST* creation_hash,
                                   TPMT_TK_CREATION* creation_ticket) {
  CHECK(creation_data) << "CreationData not defined.";
  CHECK(creation_hash) << "CreationHash not defined.";
  CHECK(creation_ticket) << "CreationTicket not defined.";
  if (creation_blob.empty()) {
    return false;
  }
  std::string mutable_creation_blob = creation_blob;
  TPM_RC result =
      Parse_TPM2B_CREATION_DATA(&mutable_creation_blob, creation_data, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing creation_data: " << GetErrorString(result);
    return false;
  }
  if (!creation_data->size) {
    LOG(ERROR) << "Error parsing creation_data: Empty data";
    return false;
  }
  result = Parse_TPM2B_DIGEST(&mutable_creation_blob, creation_hash, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing creation_hash: " << GetErrorString(result);
    return false;
  }
  result =
      Parse_TPMT_TK_CREATION(&mutable_creation_blob, creation_ticket, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing creation_ticket: " << GetErrorString(result);
    return false;
  }
  if (!mutable_creation_blob.empty()) {
    LOG(ERROR) << "Extra data at the end of the creation blob.";
    return false;
  }
  return true;
}

}  // namespace trunks
