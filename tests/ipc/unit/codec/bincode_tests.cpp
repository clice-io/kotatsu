#include <cstddef>
#include <string>
#include <variant>

#include "ipc/harness/codec_bincode.h"
#include "ipc/harness/codec_kit.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::ipc {

namespace {

using protocol::ErrorCode;
using test::code_of;

/// A request as BincodeCodec writes it.
std::string encoded_request() {
    BincodeCodec codec;
    auto encoded = codec.encode_request(1, "test/add", test::AddParams{.a = 1, .b = 2});
    return encoded ? *encoded : std::string();
}

ZEST_SUITE(ipc_codec_bincode) {

ZEST_CASE_GROUP(protocol) {
    test::codec_protocol(test::CodecKit<test::BincodeAdapter>{add_case});
}

ZEST_CASE(error_response_roundtrip_keeps_the_data) {
    test::error_response_roundtrip_keeps_the_data<test::BincodeAdapter>();
}

// A message cut inside its envelope cannot be read; one cut inside its
// params reads, but its params do not decode.
ZEST_CASE(truncated_message_fails) {
    auto message = encoded_request();
    // The params end the request.
    const auto params_size = test::BincodeAdapter::encode(test::AddParams{}).size();
    ZASSERT(message.size() > params_size);
    const auto envelope_size = message.size() - params_size;
    BincodeCodec codec;
    for(std::size_t size = 0; size < message.size(); ++size) {
        ZEST_CONTEXT("the first {} of {} bytes", size, message.size());
        auto parsed = codec.parse_message(message.substr(0, size));
        if(size < envelope_size) {
            const auto* failure = std::get_if<IncomingParseError>(&parsed);
            ZASSERT(failure != nullptr);
            ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
            continue;
        }
        const auto* request = std::get_if<IncomingRequest>(&parsed);
        ZASSERT(request != nullptr);
        auto params = codec.deserialize_value<test::AddParams>(request->params);
        ZEXPECT(!params.has_value());
    }
}

// The envelope's alternative index is its first four bytes; there are four.
ZEST_CASE(unknown_alternative_fails) {
    auto message = encoded_request();
    ZASSERT(!message.empty());
    message[0] = '\x04';
    BincodeCodec codec;
    auto parsed = codec.parse_message(message);
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
}

ZEST_CASE(params_with_trailing_bytes_fail_to_decode) {
    auto message = encoded_request();
    ZASSERT(!message.empty());
    message.push_back('\0');
    BincodeCodec codec;
    auto parsed = codec.parse_message(message);
    const auto* request = std::get_if<IncomingRequest>(&parsed);
    ZASSERT(request != nullptr);
    auto params = codec.deserialize_value<test::AddParams>(request->params);
    ZASSERT(!params.has_value());
    ZEXPECT(zest::contains(params.error().message, "trailing bytes"));
}

// An error has no params or result to take the rest of its message.
ZEST_CASE(error_with_trailing_bytes_fails) {
    BincodeCodec codec;
    auto message = codec.encode_error_response(1, Error(ErrorCode::InternalError, "x"));
    ZASSERT(message.has_value());
    message->push_back('\0');
    auto parsed = codec.parse_message(*message);
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ZASSERT(failure != nullptr);
    ZEXPECT(code_of(failure->error) == ErrorCode::ParseError);
}

};  // ZEST_SUITE(ipc_codec_bincode)

}  // namespace

}  // namespace kota::ipc
