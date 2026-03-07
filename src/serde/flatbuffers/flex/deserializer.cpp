#include "eventide/serde/flatbuffers/flex_deserializer.h"

namespace eventide::serde::flex {

Deserializer::DeserializeArray::DeserializeArray(Deserializer& deserializer,
                                                 ::flexbuffers::Vector vector,
                                                 std::size_t expected_length,
                                                 bool is_strict_length) :
    deserializer(deserializer), vector(vector), expected_length(expected_length),
    is_strict_length(is_strict_length) {}

auto Deserializer::DeserializeArray::has_next() -> result_t<bool> {
    if(!deserializer.is_valid) {
        return std::unexpected(deserializer.current_error());
    }
    return consumed_count < vector.size();
}

auto Deserializer::DeserializeArray::skip_element() -> status_t {
    auto has_next_result = has_next();
    if(!has_next_result) {
        return std::unexpected(has_next_result.error());
    }
    if(!*has_next_result) {
        deserializer.mark_invalid();
        return std::unexpected(deserializer.current_error());
    }

    ++consumed_count;
    return {};
}

auto Deserializer::DeserializeArray::end() -> status_t {
    if(!deserializer.is_valid) {
        return std::unexpected(deserializer.current_error());
    }

    if(is_strict_length) {
        if(consumed_count != expected_length || consumed_count != vector.size()) {
            deserializer.mark_invalid(error_code::type_mismatch);
            return std::unexpected(deserializer.current_error());
        }
        return {};
    }

    consumed_count = vector.size();
    return {};
}

Deserializer::DeserializeObject::DeserializeObject(Deserializer& deserializer,
                                                   ::flexbuffers::Map map) :
    deserializer(deserializer), map(map), keys(this->map.Keys()), values(this->map.Values()) {
    if(keys.size() != values.size()) {
        deserializer.mark_invalid(error_code::invalid_buffer);
    }
}

auto Deserializer::DeserializeObject::next_key() -> result_t<std::optional<std::string_view>> {
    if(!deserializer.is_valid) {
        return std::unexpected(deserializer.current_error());
    }
    if(has_pending_value) {
        deserializer.mark_invalid();
        return std::unexpected(deserializer.current_error());
    }
    if(index >= keys.size()) {
        return std::optional<std::string_view>{};
    }

    pending_key.assign(keys[index].AsKey());
    pending_value = values[index];
    has_pending_value = true;
    return std::optional<std::string_view>{std::string_view(pending_key)};
}

auto Deserializer::DeserializeObject::invalid_key(std::string_view /*key_name*/) -> status_t {
    deserializer.mark_invalid(error_code::invalid_key);
    return std::unexpected(deserializer.current_error());
}

auto Deserializer::DeserializeObject::skip_value() -> status_t {
    if(!deserializer.is_valid) {
        return std::unexpected(deserializer.current_error());
    }
    if(!has_pending_value) {
        deserializer.mark_invalid();
        return std::unexpected(deserializer.current_error());
    }

    ++index;
    has_pending_value = false;
    return {};
}

auto Deserializer::DeserializeObject::end() -> status_t {
    if(!deserializer.is_valid) {
        return std::unexpected(deserializer.current_error());
    }

    if(has_pending_value) {
        ++index;
        has_pending_value = false;
    }
    index = keys.size();
    return {};
}

Deserializer::Deserializer(std::span<const std::uint8_t> bytes) :
    buffer_storage(bytes.begin(), bytes.end()) {
    initialize();
}

Deserializer::Deserializer(std::span<const std::byte> bytes) {
    if(!bytes.empty()) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        buffer_storage.assign(data, data + bytes.size());
    }
    initialize();
}

Deserializer::Deserializer(const std::vector<std::uint8_t>& bytes) : buffer_storage(bytes) {
    initialize();
}

bool Deserializer::valid() const {
    return is_valid;
}

auto Deserializer::error() const -> error_type {
    if(is_valid) {
        return error_code::none;
    }
    return current_error();
}

auto Deserializer::finish() -> status_t {
    if(!is_valid) {
        return std::unexpected(current_error());
    }
    if(!root_consumed) {
        mark_invalid(error_code::root_not_consumed);
        return std::unexpected(current_error());
    }
    return {};
}

auto Deserializer::deserialize_none() -> result_t<bool> {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }

    const bool is_none = reference->IsNull();
    if(is_none) {
        consume_root_if_needed();
    }
    return is_none;
}

auto Deserializer::deserialize_bool(bool& value) -> status_t {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(!reference->IsBool()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    value = reference->AsBool();
    consume_root_if_needed();
    return {};
}

auto Deserializer::deserialize_char(char& value) -> status_t {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(!reference->IsString() && !reference->IsKey()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    auto text = reference->AsString();
    if(text.length() != 1) {
        mark_invalid(error_code::invalid_char);
        return std::unexpected(current_error());
    }

    value = text.c_str()[0];
    consume_root_if_needed();
    return {};
}

auto Deserializer::deserialize_str(std::string& value) -> status_t {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(!reference->IsString() && !reference->IsKey()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    auto text = reference->AsString();
    value.assign(text.c_str(), text.length());
    consume_root_if_needed();
    return {};
}

auto Deserializer::deserialize_bytes(std::vector<std::byte>& value) -> status_t {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }

    value.clear();
    if(reference->IsBlob() || reference->IsString()) {
        auto blob = reference->AsBlob();
        value.reserve(blob.size());
        for(std::size_t i = 0; i < blob.size(); ++i) {
            value.push_back(static_cast<std::byte>(blob.data()[i]));
        }

        consume_root_if_needed();
        return {};
    }

    if(reference->IsMap() || !reference->IsVector()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    auto vector = reference->AsVector();
    value.reserve(vector.size());
    for(std::size_t i = 0; i < vector.size(); ++i) {
        const auto element = vector[i];
        std::uint64_t parsed = 0;
        if(element.IsUInt()) {
            parsed = element.AsUInt64();
        } else if(element.IsInt()) {
            const auto signed_value = element.AsInt64();
            if(signed_value < 0) {
                mark_invalid(error_code::number_out_of_range);
                return std::unexpected(current_error());
            }
            parsed = static_cast<std::uint64_t>(signed_value);
        } else {
            mark_invalid(error_code::type_mismatch);
            return std::unexpected(current_error());
        }

        if(parsed > static_cast<std::uint64_t>(std::numeric_limits<std::uint8_t>::max())) {
            mark_invalid(error_code::number_out_of_range);
            return std::unexpected(current_error());
        }
        value.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(parsed)));
    }

    consume_root_if_needed();
    return {};
}

auto Deserializer::deserialize_seq(std::optional<std::size_t> len) -> result_t<DeserializeSeq> {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(reference->IsMap() || !reference->IsVector()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    consume_root_if_needed();
    return DeserializeSeq(*this, reference->AsVector(), len.value_or(0), false);
}

auto Deserializer::deserialize_tuple(std::size_t len) -> result_t<DeserializeTuple> {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(reference->IsMap() || !reference->IsVector()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    consume_root_if_needed();
    return DeserializeTuple(*this, reference->AsVector(), len, true);
}

auto Deserializer::deserialize_map(std::optional<std::size_t> /*len*/) -> result_t<DeserializeMap> {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(!reference->IsMap()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    consume_root_if_needed();
    DeserializeMap out(*this, reference->AsMap());
    if(!is_valid) {
        return std::unexpected(current_error());
    }
    return out;
}

auto Deserializer::deserialize_struct(std::string_view /*name*/, std::size_t /*len*/)
    -> result_t<DeserializeStruct> {
    auto reference = active_reference();
    if(!reference) {
        return std::unexpected(reference.error());
    }
    if(!reference->IsMap()) {
        mark_invalid(error_code::type_mismatch);
        return std::unexpected(current_error());
    }

    consume_root_if_needed();
    DeserializeStruct out(*this, reference->AsMap());
    if(!is_valid) {
        return std::unexpected(current_error());
    }
    return out;
}

Deserializer::value_scope::value_scope(Deserializer& deserializer,
                                       const ::flexbuffers::Reference& reference) :
    deserializer(deserializer), previous(deserializer.current_value), value(reference) {
    deserializer.current_value = &value;
}

Deserializer::value_scope::~value_scope() {
    deserializer.current_value = previous;
}

auto Deserializer::active_reference() -> result_t<::flexbuffers::Reference> {
    if(!is_valid) {
        return std::unexpected(current_error());
    }
    if(current_value != nullptr) {
        return *current_value;
    }
    if(root_consumed) {
        mark_invalid();
        return std::unexpected(current_error());
    }
    return root_reference;
}

void Deserializer::consume_root_if_needed() {
    if(current_value == nullptr) {
        root_consumed = true;
    }
}

void Deserializer::initialize() {
    if(buffer_storage.empty()) {
        mark_invalid(error_code::invalid_buffer);
        return;
    }
    if(!::flexbuffers::VerifyBuffer(buffer_storage.data(), buffer_storage.size())) {
        mark_invalid(error_code::invalid_buffer);
        return;
    }

    root_reference = ::flexbuffers::GetRoot(buffer_storage.data(), buffer_storage.size());
}

void Deserializer::set_error(error_type error) {
    if(last_error == error_code::none) {
        last_error = error;
    }
}

void Deserializer::mark_invalid(error_type error) {
    is_valid = false;
    set_error(error);
}

auto Deserializer::current_error() const -> error_type {
    if(last_error != error_code::none) {
        return last_error;
    }
    return error_code::unknown;
}

}  // namespace eventide::serde::flex
