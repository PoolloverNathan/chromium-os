// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A PKCS #11 backed KeyStore implementation.

#ifndef CRYPTOHOME_PKCS11_KEYSTORE_H_
#define CRYPTOHOME_PKCS11_KEYSTORE_H_

#include "keystore.h"

#include <string>

#include <base/basictypes.h>
#include <base/memory/scoped_ptr.h>
#include <chaps/pkcs11/cryptoki.h>
#include <chromeos/secure_blob.h>

namespace cryptohome {

// This class uses a PKCS #11 token as storage for key data.  The key data is
// stored in data objects with the following attributes:
// CKA_CLASS - CKO_DATA
// CKA_LABEL - A key name.
// CKA_VALUE - Binary key data (opaque to this class and the PKCS #11 token).
// CKA_APPLICATION - A constant value associated with this class.
// CKA_TOKEN - True
// CKA_PRIVATE - True
// CKA_MODIFIABLE - False
// There is no barrier between the objects created by this class and any other
// objects residing in the same token.  In practice, this means that any
// component with access to the PKCS #11 token also has access to read or delete
// key data.
class Pkcs11KeyStore : public KeyStore {
 public:
  Pkcs11KeyStore();
  virtual ~Pkcs11KeyStore();

  // KeyStore interface.
  virtual bool Read(const std::string& key_name,
                    chromeos::SecureBlob* key_data);
  virtual bool Write(const std::string& key_name,
                     const chromeos::SecureBlob& key_data);

 private:
  // Searches for a PKCS #11 object for a given key name.  If one exists, the
  // object handle is returned, otherwise CK_INVALID_HANDLE is returned.
  CK_OBJECT_HANDLE FindObject(CK_SESSION_HANDLE session_handle,
                              const std::string& key_name);

  DISALLOW_COPY_AND_ASSIGN(Pkcs11KeyStore);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PKCS11_KEYSTORE_H_
