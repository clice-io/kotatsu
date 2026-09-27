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
    auto encoded =
        codec.encode_request(1, "test/add", test::BincodeAdapter::encode(test::AddParams{}));
    return encoded ? *encoded : std::string();
}

ZEST_SUITE(ipc_codec_bincode) {

ZEST_CASE_GROUP(protocol) {
    test::codec_protocol(test::CodecKit<test::BincodeAdapter>{add_case});
}

ZEST_CASE(error_response_roundtrip_keeps_the_data) {
    test::error_response_roundtrip_keeps_the_data<test::BincodeAdapter>();
}

ZEST_CASE(truncated_message_fails) {
    auto message = encoded_request();
    ASSERT(!message.empty());
    BincodeCodec codec;
    for(std::size_t size = 0; size < message.size(); ++size) {
        ZEST_CONTEXT("the first {} of {} bytes", size, message.size());
        auto parsed = codec.parse_message(std::string_view(message).substr(0, size));
        const auto* failure = std::get_if<IncomingParseError>(&parsed);
        ASSERT(failure != nullptr);
        EXPECT(code_of(failure->error) == ErrorCode::ParseError);
    }
}

// The envelope's alternative index is its first four bytes; there are four.
ZEST_CASE(unknown_alternative_fails) {
    auto message = encoded_request();
    ASSERT(!message.empty());
    message[0] = '\x04';
    BincodeCodec codec;
    auto parsed = codec.parse_message(message);
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ASSERT(failure != nullptr);
    EXPECT(code_of(failure->error) == ErrorCode::ParseError);
}

ZEST_CASE(trailing_bytes_fails) {
    auto message = encoded_request();
    ASSERT(!message.empty());
    message.push_back('\0');
    BincodeCodec codec;
    auto parsed = codec.parse_message(message);
    const auto* failure = std::get_if<IncomingParseError>(&parsed);
    ASSERT(failure != nullptr);
    EXPECT(code_of(failure->error) == ErrorCode::ParseError);
}

};  // ZEST_SUITE(ipc_codec_bincode)

}  // namespace

}  // namespace kota::ipc
