// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains auxiliary definitions for the autogenerated structs.
// The clients should use Serialize()/Deserialize() methods on the autogenerated
// structs instead of these auxiliary definitions.
//
// Example:
//   AuthBlockState state = {/* some data */};
//   std::optional<brillo::Blob> blob = state.Serialize();
//   // The blob would have value when the serialization success.
//
//   std::optional<AuthBlockState> state2 =
//       AuthBlockState::Deserialize(blob.value());
//   // The state2 would have value when the deserialization success.

#ifndef LIBHWSEC_FOUNDATION_FLATBUFFERS_BASIC_OBJECTS_H_
#define LIBHWSEC_FOUNDATION_FLATBUFFERS_BASIC_OBJECTS_H_

#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <brillo/secure_blob.h>
#include <flatbuffers/flatbuffers.h>

namespace hwsec_foundation {

// A helper struct to indicate the output type of ToFlatBuffer.
struct IsUnionEnum {};

// ToFlatBuffer would convert the input type to a type that can be pass into
// the flatbuffer builder directly.
//
// Template parameters:
//   |T| - The type that need to be convert to FlatBuffer object.
//   |Union| - The type indicates the output should be union offset or union
//             type enum. "void" indicates union offset, "IsUnionEnum" indicates
//             union type enum. This field should be "void" for non-union input
//             type.
//   |Enable| - The enable_if_t helper field.
template <typename T, typename Union = void, typename Enable = void>
struct ToFlatBuffer {
  // You have to have a specialization for ToFlatBuffer.
  ToFlatBuffer() = delete;
};

// FromFlatBuffer would convert a flatbuffer type to the specified output type.
//
// Template parameters:
//   |T| - The type that need to be convert from FlatBuffer object.
//   |Enable| - The enable_if_t helper field.
template <typename T, typename Enable = void>
struct FromFlatBuffer {
  // You have to have a specialization for FromFlatBuffer.
  FromFlatBuffer() = delete;
};

// Helper template to convert enums to the underlying type.
template <typename T, typename = void>
struct ToUnderlying {
  using Type = T;
};

template <typename T>
struct ToUnderlying<T, std::enable_if_t<std::is_enum_v<T>>> {
  using Type = std::underlying_type_t<T>;
};

template <typename T>
using ToUnderlyingType = typename ToUnderlying<T>::Type;

// Converts std::optional<> to flatbuffers::Offset<>.
// Example:
//   std::optional<Table> => flatbuffers::Offset<_serialized_::Table>
//   std::optional<std::vector<uint32_t>> => flatbuffers::Offset<
//                                             flatbuffers::Vector<uint32_t>>
//   std::optional<SecureBlob> => flatbuffers::Offset<
//                                  flatbuffers::Vector<uint8_t>>
//   std::optional<std::string> => flatbuffers::Offset<flatbuffers::String>
template <typename T, typename Union>
struct ToFlatBuffer<std::optional<T>,
                    Union,
                    std::enable_if_t<!std::is_scalar_v<T>>> {
  using ResultType = typename ToFlatBuffer<T, Union>::ResultType;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const std::optional<T>& object) const {
    if (object.has_value()) {
      return ToFlatBuffer<T, Union>()(builder, object.value());
    }
    return ResultType(0);  // zero offset
  }
};

// Converts std::optional<> to flatbuffers::Optional<>.
// Example:
//   std::optional<uint32_t> => flatbuffers::Optional<uint32_t>
//   std::optional<Enum> => flatbuffers::Optional<_serialized_::Enum>
template <typename T>
struct ToFlatBuffer<std::optional<T>,
                    void,
                    std::enable_if_t<std::is_scalar_v<T>>> {
  using ResultType =
      flatbuffers::Optional<typename ToFlatBuffer<T>::ResultType>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const std::optional<T>& object) const {
    if (object.has_value()) {
      return ToFlatBuffer<T>()(builder, object.value());
    }
    return flatbuffers::nullopt;
  }
};

// Converts std::vector<> to flatbuffers::Offset<flatbuffers::Vector<>>.
// And excludes the std::vector<uint8_t> for the blob type.
// Example:
//   std::vector<uint32_t> => flatbuffers::Offset<flatbuffers::Vector<uint32_t>>
//   std::vector<Enum> => flatbuffers::Offset<flatbuffers::Vector<
//                            std::underlying_type_t<Enum>>>
//   std::vector<Table> => flatbuffers::Offset<flatbuffers::Vector<
//                             flatbuffers::Offset<_serialized_::Table>>>
template <typename T>
struct ToFlatBuffer<std::vector<T>,
                    void,
                    std::enable_if_t<!std::is_same_v<T, uint8_t>>> {
  using UnderlyingType = ToUnderlyingType<typename ToFlatBuffer<T>::ResultType>;
  using ResultType = flatbuffers::Offset<flatbuffers::Vector<UnderlyingType>>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const std::vector<T>& object) const {
    std::vector<UnderlyingType> result;
    result.reserve(object.size());

    for (const auto& inner : object) {
      result.push_back(
          static_cast<UnderlyingType>(ToFlatBuffer<T>()(builder, inner)));
    }
    return builder->CreateVector(result);
  }
};

// Converts std::variant<> to flatbuffers::Offset<void>.
// Example:
//   std::variant<std::monostate, TableA, TableB> => flatbuffers::Offset<void>
template <typename... VariantArgs>
struct ToFlatBuffer<std::variant<VariantArgs...>> {
  using ResultType = flatbuffers::Offset<void>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const std::variant<VariantArgs...>& object) const {
    return std::visit(
        [builder](const auto& arg) -> ResultType {
          using ArgType = std::decay_t<decltype(arg)>;
          auto result = ToFlatBuffer<ArgType>()(builder, arg);
          return result.Union();
        },
        object);
  }
};

// Converts arithmetic type to arithmetic type.
// Example:
//   bool => bool
//   uint32_t => uint32_t
//   float => float
template <typename T>
struct ToFlatBuffer<T, void, std::enable_if_t<std::is_arithmetic_v<T>>> {
  using ResultType = T;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        T object) const {
    return object;
  }
};

// Converts std::string to flatbuffers::Offset<flatbuffers::String>.
// Example:
//   std::string => flatbuffers::Offset<flatbuffers::String>
template <>
struct ToFlatBuffer<std::string> {
  using ResultType = flatbuffers::Offset<flatbuffers::String>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const std::string& object) const {
    return builder->CreateString(object);
  }
};

// Converts Blob to flatbuffers::Offset<flatbuffers::Vector<uint8_t>>.
// This specialization could prevent creating an extra copy of the blob compares
// to the generic vector specialization.
template <>
struct ToFlatBuffer<brillo::Blob> {
  using ResultType = flatbuffers::Offset<flatbuffers::Vector<uint8_t>>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const brillo::Blob& object) const {
    return builder->CreateVector(object.data(), object.size());
  }
};

// Converts SecureBlob to flatbuffers::Offset<flatbuffers::Vector<uint8_t>>.
// This specialization could prevent creating an extra copy of the blob compares
// to the generic vector specialization.
template <>
struct ToFlatBuffer<brillo::SecureBlob> {
  using ResultType = flatbuffers::Offset<flatbuffers::Vector<uint8_t>>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        const brillo::SecureBlob& object) const {
    return builder->CreateVector(object.data(), object.size());
  }
};

// Converts std::monostate to flatbuffers::Offset<void>.
// This specialization is needed for converting an empty variant.
template <>
struct ToFlatBuffer<std::monostate> {
  using ResultType = flatbuffers::Offset<void>;

  ResultType operator()(flatbuffers::FlatBufferBuilder* builder,
                        std::monostate) const {
    return ResultType(0);  // zero offset
  }
};

// Converts std::optional<> from flatbuffers::Optional or pointer.
// Example:
//   flatbuffers::Optional<uint32_t> => std::optional<uint32_t>
//   flatbuffers::Optional<_serialized_::Enum> => std::optional<Enum>
//   flatbuffers::Vector<uint32_t>* => std::optional<std::vector<uint32_t>>
//   flatbuffers::String* => std::optional<std::String>
//   flatbuffers::Vector<uint8_t>* => std::optional<SecureBlob>
template <typename T>
struct FromFlatBuffer<std::optional<T>> {
  template <typename InputType>
  std::optional<T> operator()(const InputType* object) const {
    if (object == nullptr) {
      return std::nullopt;
    }
    return FromFlatBuffer<T>()(object);
  }

  template <typename InputType>
  std::optional<T> operator()(
      const flatbuffers::Optional<InputType>& object) const {
    if (!object.has_value()) {
      return std::nullopt;
    }
    return FromFlatBuffer<T>()(object.value());
  }
};

// Converts std::vector<> from flatbuffers::Vector<>*.
// And excludes the std::vector<uint8_t> for the blob type.
// Example:
//   flatbuffers::Vector<uint32_t>* => std::vector<uint32_t>
//   flatbuffers::Vector<_serialized_::Enum>* => std::vector<Enum>
//   flatbuffers::Vector<_serialized_::Table>* => std::vector<Table>
template <typename T>
struct FromFlatBuffer<std::vector<T>,
                      std::enable_if_t<!std::is_same_v<T, uint8_t>>> {
  template <typename InputType>
  std::vector<T> operator()(const InputType* object) const {
    if (object == nullptr) {
      return std::vector<T>();
    }

    std::vector<T> result;
    result.reserve(object->size());

    for (const auto& inner : *object) {
      result.push_back(FromFlatBuffer<T>()(inner));
    }
    return result;
  }
};

// Converts arithmetic type from arithmetic type.
// Example:
//   bool => bool
//   uint32_t => uint32_t
//   float => float
template <typename T>
struct FromFlatBuffer<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
  T operator()(T object) const { return object; }
};

// Converts std::string from flatbuffers::String*.
template <>
struct FromFlatBuffer<std::string> {
  std::string operator()(const flatbuffers::String* object) const {
    if (object == nullptr) {
      return std::string();
    }
    return object->str();
  }
};

// Converts brillo::Blob from flatbuffers::Vector<uint8_t>*.
template <>
struct FromFlatBuffer<brillo::Blob> {
  brillo::Blob operator()(const flatbuffers::Vector<uint8_t>* object) const {
    if (object == nullptr) {
      return brillo::Blob();
    }
    return brillo::Blob(object->data(), object->data() + object->size());
  }
};

// Converts brillo::SecureBlob from flatbuffers::Vector<uint8_t>*.
template <>
struct FromFlatBuffer<brillo::SecureBlob> {
  brillo::SecureBlob operator()(
      const flatbuffers::Vector<uint8_t>* object) const {
    if (object == nullptr) {
      return brillo::SecureBlob();
    }
    return brillo::SecureBlob(object->data(), object->data() + object->size());
  }
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_FLATBUFFERS_BASIC_OBJECTS_H_
