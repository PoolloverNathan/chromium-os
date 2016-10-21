//
// Copyright (C) 2012 The Android Open Source Project
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

#ifndef SHILL_CALLBACKS_H_
#define SHILL_CALLBACKS_H_

#include <string>
#include <vector>

#include <base/callback.h>

#include "shill/accessor_interface.h"
#include "shill/key_value_store.h"

namespace shill {

class Error;
// Convenient typedefs for some commonly used callbacks.
using ResultCallback = base::Callback<void(const Error&)>;
using ResultBoolCallback = base::Callback<void(const Error&, bool)>;
using ResultStringCallback =
    base::Callback<void(const Error&, const std::string&)>;
using EnabledStateChangedCallback = base::Callback<void(const Error&)>;
using KeyValueStoreCallback =
    base::Callback<void(const KeyValueStore&, const Error&)>;
using KeyValueStoresCallback =
    base::Callback<void(const std::vector<KeyValueStore>&, const Error&)>;
using RpcIdentifierCallback =
    base::Callback<void(const std::string&, const Error&)>;
using StringCallback = base::Callback<void(const std::string&, const Error&)>;
using ActivationStateSignalCallback =
    base::Callback<void(uint32_t, uint32_t, const KeyValueStore&)>;
using ResultStringmapsCallback =
    base::Callback<void(const Stringmaps&, const Error&)>;
using BrilloAnyCallback =
    base::Callback<void(const std::map<uint32_t, brillo::Any>&,
                        const Error&)>;

}  // namespace shill

#endif  // SHILL_CALLBACKS_H_
