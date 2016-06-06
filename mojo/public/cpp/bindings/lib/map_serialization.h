// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_MAP_SERIALIZATION_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_MAP_SERIALIZATION_H_

#include <type_traits>

#include "mojo/public/cpp/bindings/array.h"
#include "mojo/public/cpp/bindings/lib/array_serialization.h"
#include "mojo/public/cpp/bindings/lib/map_data_internal.h"
#include "mojo/public/cpp/bindings/lib/serialization_forward.h"
#include "mojo/public/cpp/bindings/map.h"

namespace mojo {
namespace internal {

template <typename MaybeConstUserType>
class MapReaderBase {
 public:
  using UserType = typename std::remove_const<MaybeConstUserType>::type;
  using Traits = MapTraits<UserType>;
  using MaybeConstIterator =
      decltype(Traits::GetBegin(std::declval<MaybeConstUserType&>()));

  explicit MapReaderBase(MaybeConstUserType& input)
      : input_(input), iter_(Traits::GetBegin(input_)) {}
  ~MapReaderBase() {}

  size_t GetSize() const { return Traits::GetSize(input_); }

  // Return null because key or value elements are not stored continuously in
  // memory.
  void* GetDataIfExists() { return nullptr; }

 protected:
  MaybeConstUserType& input_;
  MaybeConstIterator iter_;
};

// Used as the UserTypeReader template parameter of ArraySerializer.
template <typename MaybeConstUserType>
class MapKeyReader : public MapReaderBase<MaybeConstUserType> {
 public:
  using Base = MapReaderBase<MaybeConstUserType>;
  using Traits = typename Base::Traits;

  explicit MapKeyReader(MaybeConstUserType& input) : Base(input) {}
  ~MapKeyReader() {}

  const typename Traits::Key& GetNext() {
    const typename Traits::Key& key = Traits::GetKey(this->iter_);
    Traits::AdvanceIterator(this->iter_);
    return key;
  }
};

// Used as the UserTypeReader template parameter of ArraySerializer.
template <typename MaybeConstUserType>
class MapValueReader : public MapReaderBase<MaybeConstUserType> {
 public:
  using Base = MapReaderBase<MaybeConstUserType>;
  using Traits = typename Base::Traits;
  using MaybeConstIterator = typename Base::MaybeConstIterator;

  explicit MapValueReader(MaybeConstUserType& input) : Base(input) {}
  ~MapValueReader() {}

  using GetNextResult =
      decltype(Traits::GetValue(std::declval<MaybeConstIterator&>()));
  GetNextResult GetNext() {
    GetNextResult value = Traits::GetValue(this->iter_);
    Traits::AdvanceIterator(this->iter_);
    return value;
  }
};

template <typename Key, typename Value, typename MaybeConstUserType>
struct Serializer<Map<Key, Value>, MaybeConstUserType> {
  using UserType = typename std::remove_const<MaybeConstUserType>::type;
  using Traits = MapTraits<UserType>;
  using UserKey = typename Traits::Key;
  using UserValue = typename Traits::Value;
  using Data = typename Map<Key, Value>::Data_;
  using KeyArraySerializer = ArraySerializer<Array<Key>,
                                             Array<UserKey>,
                                             MapKeyReader<MaybeConstUserType>>;
  using ValueArraySerializer =
      ArraySerializer<Array<Value>,
                      Array<UserValue>,
                      MapValueReader<MaybeConstUserType>>;

  static size_t PrepareToSerialize(MaybeConstUserType& input,
                                   SerializationContext* context) {
    if (CallIsNullIfExists<Traits>(input))
      return 0;

    size_t struct_overhead = sizeof(Data);
    MapKeyReader<MaybeConstUserType> key_reader(input);
    size_t keys_size =
        KeyArraySerializer::GetSerializedSize(&key_reader, context);
    MapValueReader<MaybeConstUserType> value_reader(input);
    size_t values_size =
        ValueArraySerializer::GetSerializedSize(&value_reader, context);

    return struct_overhead + keys_size + values_size;
  }

  // We don't need an ArrayValidateParams instance for key validation since
  // we can deduce it from the Key type. (which can only be primitive types or
  // non-nullable strings.)
  static void Serialize(MaybeConstUserType& input,
                        Buffer* buf,
                        Data** output,
                        const ArrayValidateParams* validate_params,
                        SerializationContext* context) {
    DCHECK(validate_params->key_validate_params);
    DCHECK(validate_params->element_validate_params);
    if (CallIsNullIfExists<Traits>(input)) {
      *output = nullptr;
      return;
    }

    auto result = Data::New(buf);
    if (result) {
      result->keys.ptr = Array<Key>::Data_::New(Traits::GetSize(input), buf);
      if (result->keys.ptr) {
        MapKeyReader<MaybeConstUserType> key_reader(input);
        KeyArraySerializer::SerializeElements(
            &key_reader, buf, result->keys.ptr,
            validate_params->key_validate_params, context);
      }

      result->values.ptr =
          Array<Value>::Data_::New(Traits::GetSize(input), buf);
      if (result->values.ptr) {
        MapValueReader<MaybeConstUserType> value_reader(input);
        ValueArraySerializer::SerializeElements(
            &value_reader, buf, result->values.ptr,
            validate_params->element_validate_params, context);
      }
    }
    *output = result;
  }

  static bool Deserialize(Data* input,
                          UserType* output,
                          SerializationContext* context) {
    if (!input)
      return CallSetToNullIfExists<Traits>(output);

    Array<UserKey> keys;
    Array<UserValue> values;

    if (!KeyArraySerializer::DeserializeElements(input->keys.ptr, &keys,
                                                 context) ||
        !ValueArraySerializer::DeserializeElements(input->values.ptr, &values,
                                                   context)) {
      return false;
    }

    DCHECK_EQ(keys.size(), values.size());
    size_t size = keys.size();
    Traits::SetToEmpty(output);

    for (size_t i = 0; i < size; ++i)
      Traits::Insert(*output, std::move(keys[i]), std::move(values[i]));
    return true;
  }
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_MAP_SERIALIZATION_H_
