#include "kota/ipc/blocking_client.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace kota::ipc::detail {

namespace {

using native_handle_type = BlockingPipe::native_handle_type;

/// What a moved-from BlockingPipe holds.
#ifdef _WIN32
constexpr native_handle_type no_handle = nullptr;
#else
constexpr native_handle_type no_handle = -1;
#endif

#ifdef _WIN32

std::error_code last_error() {
    return std::error_code(static_cast<int>(::GetLastError()), std::system_category());
}

/// Reads what the pipe holds, up to the size of `buffer`, once it holds a
/// byte: how many bytes, zero once the other end has closed.
std::expected<std::size_t, std::error_code> read_some(native_handle_type handle,
                                                      std::span<char> buffer) {
    DWORD got = 0;
    if(!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
        if(::GetLastError() == ERROR_BROKEN_PIPE) {
            return 0;
        }
        return std::unexpected(last_error());
    }
    return got;
}

/// Writes all of `bytes`.
std::error_code write_all(native_handle_type handle, std::string_view bytes) {
    while(!bytes.empty()) {
        // WriteFile takes no more bytes than a DWORD counts.
        const auto size = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), MAXDWORD));
        DWORD written = 0;
        if(!::WriteFile(handle, bytes.data(), size, &written, nullptr)) {
            return last_error();
        }
        bytes.remove_prefix(written);
    }
    return {};
}

#else

std::error_code last_error() {
    return std::error_code(errno, std::generic_category());
}

std::expected<std::size_t, std::error_code> read_some(native_handle_type handle,
                                                      std::span<char> buffer) {
    while(true) {
        const auto got = ::recv(handle, buffer.data(), buffer.size(), 0);
        if(got >= 0) {
            return static_cast<std::size_t>(got);
        }
        if(errno != EINTR) {
            return std::unexpected(last_error());
        }
    }
}

std::error_code write_all(native_handle_type handle, std::string_view bytes) {
    // macOS has no MSG_NOSIGNAL: the constructor has the socket itself not
    // raise SIGPIPE.
#ifdef __APPLE__
    constexpr int flags = 0;
#else
    constexpr int flags = MSG_NOSIGNAL;
#endif
    while(!bytes.empty()) {
        const auto written = ::send(handle, bytes.data(), bytes.size(), flags);
        if(written < 0) {
            if(errno == EINTR) {
                continue;
            }
            return last_error();
        }
        bytes.remove_prefix(static_cast<std::size_t>(written));
    }
    return {};
}

#endif

}  // namespace

BlockingPipe::BlockingPipe(native_handle_type handle, std::size_t max_payload) noexcept :
    handle(handle), parser(max_payload) {
#ifdef __APPLE__
    int on = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

BlockingPipe::BlockingPipe(BlockingPipe&& other) noexcept :
    handle(std::exchange(other.handle, no_handle)), parser(std::move(other.parser)),
    unread(std::move(other.unread)) {}

BlockingPipe& BlockingPipe::operator=(BlockingPipe&& other) noexcept {
    if(this != &other) {
        close();
        handle = std::exchange(other.handle, no_handle);
        parser = std::move(other.parser);
        unread = std::move(other.unread);
    }
    return *this;
}

BlockingPipe::~BlockingPipe() {
    close();
}

void BlockingPipe::close() noexcept {
    if(handle == no_handle) {
        return;
    }
#ifdef _WIN32
    ::CloseHandle(handle);
#else
    ::close(handle);
#endif
    handle = no_handle;
}

std::error_code BlockingPipe::write(std::string_view payload) {
    if(auto failed = write_all(handle, frame_header(payload.size()))) {
        return failed;
    }
    return write_all(handle, payload);
}

std::expected<std::string, ReadError> BlockingPipe::read() {
    auto step = parser.feed(unread);
    unread.erase(0, step.consumed);
    std::array<char, 4096> buffer;
    while(!step.frame) {
        auto got = read_some(handle, buffer);
        if(!got || *got == 0) {
            return std::unexpected(ReadError{
                .kind = ReadError::Kind::Closed,
                .message = got ? "the input ended" : got.error().message(),
            });
        }
        const std::string_view chunk(buffer.data(), *got);
        step = parser.feed(chunk);
        unread.assign(chunk.substr(step.consumed));
    }
    return std::move(*step.frame);
}

std::string not_the_answer(const IncomingMessage& message) {
    return std::visit(
        [](const auto& incoming) -> std::string {
            using T = std::remove_cvref_t<decltype(incoming)>;
            if constexpr(std::is_same_v<T, IncomingRequest>) {
                return "a request instead of the answer: " + incoming.method;
            } else if constexpr(std::is_same_v<T, IncomingNotification>) {
                return "a notification instead of the answer: " + incoming.method;
            } else if constexpr(std::is_same_v<T, IncomingResponse>) {
                return "the answer to another request";
            } else if constexpr(std::is_same_v<T, IncomingErrorResponse>) {
                return (incoming.id ? "an error answering another request: "
                                    : "an error response without an id: ") +
                       incoming.error.message;
            } else {
                return incoming.error.message;
            }
        },
        message);
}

}  // namespace kota::ipc::detail
