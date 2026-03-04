#include "eventide/serde/flatbuffers/flex/serializer.h"

namespace eventide::serde::flex {

Serializer::Serializer(std::size_t initial_capacity, ::flexbuffers::BuilderFlag flags) :
    builder(initial_capacity, flags) {}

auto Serializer::view() -> result_t<std::span<const std::uint8_t>> {
    auto finalized = finalize();
    if(!finalized) {
        return std::unexpected(finalized.error());
    }

    const auto& out = builder.GetBuffer();
    return std::span<const std::uint8_t>(out.data(), out.size());
}

auto Serializer::bytes() -> result_t<std::vector<std::uint8_t>> {
    auto out = view();
    if(!out) {
        return std::unexpected(out.error());
    }
    return std::vector<std::uint8_t>(out->begin(), out->end());
}

void Serializer::clear() {
    builder.Clear();
    stack.clear();
    root_written = false;
    is_finished = false;
    is_valid = true;
    last_error = error_code::none;
}

bool Serializer::valid() const {
    return is_valid;
}

auto Serializer::error() const -> error_type {
    if(is_valid) {
        return error_code::none;
    }
    return current_error();
}

auto Serializer::serialize_null() -> result_t<value_type> {
    return write_leaf([&] { builder.Null(); },
                      [&](const std::string& key_name) { builder.Null(key_name.c_str()); });
}

auto Serializer::serialize_bool(bool value) -> result_t<value_type> {
    return write_leaf([&] { builder.Bool(value); },
                      [&](const std::string& key_name) { builder.Bool(key_name.c_str(), value); });
}

auto Serializer::serialize_int(std::int64_t value) -> result_t<value_type> {
    return write_leaf([&] { builder.Int(value); },
                      [&](const std::string& key_name) { builder.Int(key_name.c_str(), value); });
}

auto Serializer::serialize_uint(std::uint64_t value) -> result_t<value_type> {
    return write_leaf([&] { builder.UInt(value); },
                      [&](const std::string& key_name) { builder.UInt(key_name.c_str(), value); });
}

auto Serializer::serialize_float(double value) -> result_t<value_type> {
    if(!std::isfinite(value)) {
        return serialize_null();
    }

    return write_leaf(
        [&] { builder.Double(value); },
        [&](const std::string& key_name) { builder.Double(key_name.c_str(), value); });
}

auto Serializer::serialize_char(char value) -> result_t<value_type> {
    const std::string text(1, value);
    return write_leaf([&] { builder.String(text); },
                      [&](const std::string& key_name) { builder.String(key_name.c_str(), text); });
}

auto Serializer::serialize_str(std::string_view value) -> result_t<value_type> {
    return write_leaf([&] { builder.String(value.data(), value.size()); },
                      [&](const std::string& key_name) {
                          builder.Key(key_name.c_str());
                          builder.String(value.data(), value.size());
                      });
}

auto Serializer::serialize_bytes(std::string_view value) -> result_t<value_type> {
    return write_leaf(
        [&] { builder.Blob(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()); },
        [&](const std::string& key_name) {
            builder.Blob(key_name.c_str(),
                         reinterpret_cast<const std::uint8_t*>(value.data()),
                         value.size());
        });
}

auto Serializer::serialize_bytes(std::span<const std::byte> value) -> result_t<value_type> {
    return write_leaf(
        [&] { builder.Blob(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()); },
        [&](const std::string& key_name) {
            builder.Blob(key_name.c_str(),
                         reinterpret_cast<const std::uint8_t*>(value.data()),
                         value.size());
        });
}

auto Serializer::serialize_seq(std::optional<std::size_t> /*len*/) -> result_t<SerializeSeq> {
    auto started = begin_array();
    if(!started) {
        return std::unexpected(started.error());
    }
    return SerializeSeq(*this);
}

auto Serializer::serialize_tuple(std::size_t /*len*/) -> result_t<SerializeTuple> {
    auto started = begin_array();
    if(!started) {
        return std::unexpected(started.error());
    }
    return SerializeTuple(*this);
}

auto Serializer::serialize_map(std::optional<std::size_t> /*len*/) -> result_t<SerializeMap> {
    auto started = begin_object();
    if(!started) {
        return std::unexpected(started.error());
    }
    return SerializeMap(*this);
}

auto Serializer::serialize_struct(std::string_view /*name*/, std::size_t /*len*/)
    -> result_t<SerializeStruct> {
    auto started = begin_object();
    if(!started) {
        return std::unexpected(started.error());
    }
    return SerializeStruct(*this);
}

auto Serializer::begin_object() -> status_t {
    auto start = start_container(container_kind::object);
    if(!start) {
        return std::unexpected(start.error());
    }

    stack.push_back(container_frame{
        .kind = container_kind::object,
        .start = *start,
        .expect_key = true,
        .pending_key = {},
    });
    return {};
}

auto Serializer::end_object() -> result_t<value_type> {
    if(!is_valid || stack.empty()) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    const auto frame = stack.back();
    if(frame.kind != container_kind::object || !frame.expect_key) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    builder.EndMap(frame.start);
    if(builder.HasDuplicateKeys()) {
        mark_invalid(error_code::duplicate_keys);
        return std::unexpected(current_error());
    }

    stack.pop_back();
    return status();
}

auto Serializer::begin_array() -> status_t {
    auto start = start_container(container_kind::array);
    if(!start) {
        return std::unexpected(start.error());
    }

    stack.push_back(container_frame{
        .kind = container_kind::array,
        .start = *start,
        .expect_key = false,
        .pending_key = {},
    });
    return {};
}

auto Serializer::end_array() -> result_t<value_type> {
    if(!is_valid || stack.empty()) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    const auto frame = stack.back();
    if(frame.kind != container_kind::array) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    builder.EndVector(frame.start, false, false);
    stack.pop_back();
    return status();
}

auto Serializer::key(std::string_view key_name) -> status_t {
    if(!is_valid || stack.empty()) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    auto& frame = stack.back();
    if(frame.kind != container_kind::object || !frame.expect_key) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    frame.pending_key.assign(key_name.data(), key_name.size());
    frame.expect_key = false;
    return {};
}

auto Serializer::start_container(container_kind kind) -> result_t<std::size_t> {
    if(!before_value()) {
        return std::unexpected(current_error());
    }

    auto key_name = consume_parent_key();
    if(key_name.has_value()) {
        if(kind == container_kind::object) {
            return builder.StartMap(key_name->c_str());
        }
        return builder.StartVector(key_name->c_str());
    }

    if(kind == container_kind::object) {
        return builder.StartMap();
    }
    return builder.StartVector();
}

auto Serializer::consume_parent_key() -> std::optional<std::string> {
    if(stack.empty()) {
        return std::nullopt;
    }

    auto& frame = stack.back();
    if(frame.kind != container_kind::object) {
        return std::nullopt;
    }

    auto out = std::move(frame.pending_key);
    frame.pending_key.clear();
    return out;
}

bool Serializer::before_value() {
    if(!is_valid) {
        return false;
    }

    if(stack.empty()) {
        if(root_written) {
            mark_invalid();
            return false;
        }

        root_written = true;
        return true;
    }

    auto& frame = stack.back();
    if(frame.kind == container_kind::array) {
        return true;
    }

    if(frame.expect_key) {
        mark_invalid();
        return false;
    }

    frame.expect_key = true;
    return true;
}

auto Serializer::finalize() -> status_t {
    if(!is_valid) {
        return std::unexpected(current_error());
    }

    if(is_finished) {
        return {};
    }

    if(!root_written || !stack.empty()) {
        mark_invalid();
        return std::unexpected(current_error());
    }

    builder.Finish();
    is_finished = true;

    if(builder.HasDuplicateKeys()) {
        mark_invalid(error_code::duplicate_keys);
        return std::unexpected(current_error());
    }

    return {};
}

void Serializer::set_error(error_type error) {
    if(last_error == error_code::none) {
        last_error = error;
    }
}

void Serializer::mark_invalid(error_type error) {
    is_valid = false;
    set_error(error);
}

auto Serializer::current_error() const -> error_type {
    if(last_error != error_code::none) {
        return last_error;
    }
    return error_code::unknown;
}

auto Serializer::status() const -> status_t {
    if(is_valid) {
        return {};
    }
    return std::unexpected(current_error());
}

}  // namespace eventide::serde::flex
