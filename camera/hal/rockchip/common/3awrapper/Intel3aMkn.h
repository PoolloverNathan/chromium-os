/*
 * Copyright (C) 2017 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMMON_3AWRAPPER_INTEL3AMKN_H_
#define COMMON_3AWRAPPER_INTEL3AMKN_H_
#include <ia_mkn_encoder.h>

NAMESPACE_DECLARATION {
class Intel3aMkn {
public:
    Intel3aMkn();
    virtual ~Intel3aMkn();

     bool init(ia_mkn_config_bits mkn_config_bits,
                  size_t mkn_section_1_size,
                  size_t mkn_section_2_size);
    void uninit();
    ia_binary_data prepare(ia_mkn_trg data_target);
    ia_err enable(bool enable_data_collection);

    uintptr_t getMknHandle() const;
private:
    uintptr_t mMknHandle;
};
} NAMESPACE_DECLARATION_END
#endif //COMMON_3AWRAPPER_INTEL3AMKN_H_
