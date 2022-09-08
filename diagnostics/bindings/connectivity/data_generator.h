// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_DATA_GENERATOR_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_DATA_GENERATOR_H_

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <base/containers/flat_map.h>
#include <mojo/public/cpp/system/handle.h>

#include "diagnostics/bindings/connectivity/context.h"

namespace chromeos {
namespace cros_healthd {
namespace connectivity {

template <typename T>
class DataGeneratorInterface {
 public:
  using Type = T;
  // Generates T for TestConsumer and TestProvider to test the parameters. This
  // should return value even if |HasNext()| is false.
  virtual T Generate() = 0;
  // Returns true if there are values need to be generated by |Generate|. Most
  // of the cases this only returns true before the first |Generate|. Some types
  // require more than one |Generate| to test different values.
  virtual bool HasNext() = 0;
};

// Generator for primitive c++ types and string.
template <typename T>
class DataGenerator : public DataGeneratorInterface<T> {
  static_assert(std::is_same_v<T, bool> || std::is_same_v<T, int8_t> ||
                    std::is_same_v<T, uint8_t> || std::is_same_v<T, int16_t> ||
                    std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> ||
                    std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> ||
                    std::is_same_v<T, uint64_t> || std::is_same_v<T, float> ||
                    std::is_same_v<T, double> || std::is_same_v<T, std::string>,
                "Undefined DataGenerator type.");

 public:
  DataGenerator(const DataGenerator&) = delete;
  DataGenerator& operator=(const DataGenerator&) = delete;
  virtual ~DataGenerator() = default;

  static std::unique_ptr<DataGenerator> Create(Context*) {
    return std::unique_ptr<DataGenerator>(new DataGenerator<T>());
  }

 public:
  // DataGeneratorInterface overrides.
  T Generate() override {
    has_next_ = false;
    return T();
  }

  bool HasNext() override { return has_next_; }

 protected:
  DataGenerator() = default;

 private:
  bool has_next_ = true;
};

// Generator for optional types.
template <typename GeneratorType>
class OptionalGenerator : public DataGeneratorInterface<
                              std::optional<typename GeneratorType::Type>> {
 public:
  OptionalGenerator(const OptionalGenerator&) = delete;
  OptionalGenerator& operator=(const OptionalGenerator&) = delete;
  virtual ~OptionalGenerator() = default;

  static std::unique_ptr<OptionalGenerator> Create(Context* context) {
    return std::unique_ptr<OptionalGenerator>(
        new OptionalGenerator<GeneratorType>(context));
  }

 public:
  // DataGeneratorInterface overrides.
  std::optional<typename GeneratorType::Type> Generate() override {
    if (generator_->HasNext())
      return generator_->Generate();
    returned_null_ = true;
    return std::nullopt;
  }

  bool HasNext() override { return !returned_null_ || generator_->HasNext(); }

 protected:
  explicit OptionalGenerator(Context* context) {
    generator_ = GeneratorType::Create(context);
  }

 private:
  std::unique_ptr<GeneratorType> generator_;
  bool returned_null_ = false;
};

// Generator for nullable types. Notes that this generate the same type of its
// non-nullable version.
template <typename GeneratorType>
class NullableGenerator
    : public DataGeneratorInterface<typename GeneratorType::Type> {
 public:
  NullableGenerator(const NullableGenerator&) = delete;
  NullableGenerator& operator=(const NullableGenerator&) = delete;
  virtual ~NullableGenerator() = default;

  static std::unique_ptr<NullableGenerator> Create(Context* context) {
    return std::unique_ptr<NullableGenerator>(
        new NullableGenerator<GeneratorType>(context));
  }

 public:
  // DataGeneratorInterface overrides.
  typename GeneratorType::Type Generate() override {
    if (generator_->HasNext())
      return generator_->Generate();
    returned_null_ = true;
    return typename GeneratorType::Type();
  }

  bool HasNext() override { return !returned_null_ || generator_->HasNext(); }

 protected:
  explicit NullableGenerator(Context* context) {
    generator_ = GeneratorType::Create(context);
  }

 private:
  std::unique_ptr<GeneratorType> generator_;
  bool returned_null_ = false;
};

// Generator for array types.
template <typename GeneratorType>
class ArrayGenerator
    : public DataGeneratorInterface<std::vector<typename GeneratorType::Type>> {
 public:
  ArrayGenerator(const ArrayGenerator&) = delete;
  ArrayGenerator& operator=(const ArrayGenerator&) = delete;
  virtual ~ArrayGenerator() = default;

  static std::unique_ptr<ArrayGenerator> Create(Context* context) {
    return std::unique_ptr<ArrayGenerator>(
        new ArrayGenerator<GeneratorType>(context));
  }

 public:
  std::vector<typename GeneratorType::Type> Generate() override {
    std::vector<typename GeneratorType::Type> res;
    while (generator_->HasNext()) {
      res.push_back(generator_->Generate());
    }
    return res;
  }

  bool HasNext() override { return generator_->HasNext(); }

 protected:
  explicit ArrayGenerator(Context* context) {
    generator_ = GeneratorType::Create(context);
  }

 private:
  std::unique_ptr<GeneratorType> generator_;
};

// Generator for map types.
template <typename KeyGenerator, typename ValueGenerator>
class MapGenerator : public DataGeneratorInterface<
                         base::flat_map<typename KeyGenerator::Type,
                                        typename ValueGenerator::Type>> {
 public:
  MapGenerator(const MapGenerator&) = delete;
  MapGenerator& operator=(const MapGenerator&) = delete;
  virtual ~MapGenerator() = default;

  static std::unique_ptr<MapGenerator> Create(Context* context) {
    return std::unique_ptr<MapGenerator>(
        new MapGenerator<KeyGenerator, ValueGenerator>(context));
  }

 public:
  base::flat_map<typename KeyGenerator::Type, typename ValueGenerator::Type>
  Generate() override {
    base::flat_map<typename KeyGenerator::Type, typename ValueGenerator::Type>
        res;
    res[key_generator_->Generate()] = value_generator_->Generate();
    return res;
  }

  bool HasNext() override {
    return key_generator_->HasNext() || value_generator_->HasNext();
  }

 protected:
  explicit MapGenerator(Context* context) {
    key_generator_ = KeyGenerator::Create(context);
    value_generator_ = ValueGenerator::Create(context);
  }

 private:
  std::unique_ptr<KeyGenerator> key_generator_;
  std::unique_ptr<ValueGenerator> value_generator_;
};

// Generator for handle types.
class HandleDataGenerator
    : public DataGeneratorInterface<::mojo::ScopedHandle> {
 public:
  HandleDataGenerator(const HandleDataGenerator&) = delete;
  HandleDataGenerator& operator=(const HandleDataGenerator&) = delete;
  virtual ~HandleDataGenerator() = default;

  static std::unique_ptr<HandleDataGenerator> Create(Context*) {
    return std::unique_ptr<HandleDataGenerator>(new HandleDataGenerator());
  }

 public:
  // DataGeneratorInterface overrides.
  ::mojo::ScopedHandle Generate() override;

  bool HasNext() override { return has_next_; }

 protected:
  HandleDataGenerator() = default;

 private:
  bool has_next_ = true;
};

}  // namespace connectivity
}  // namespace cros_healthd
}  // namespace chromeos

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_DATA_GENERATOR_H_
