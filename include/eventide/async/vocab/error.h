#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "eventide/async/vocab/outcome.h"

namespace eventide {

class error {
public:
    constexpr error() noexcept = default;

    constexpr error(const error&) noexcept = default;

    constexpr explicit error(int code) noexcept : code(code) {}

    constexpr int value() const noexcept {
        return code;
    }

    constexpr void clear() noexcept {
        code = 0;
    }

    constexpr bool has_error() const noexcept {
        return code != 0;
    }

    constexpr explicit operator bool() const noexcept {
        return has_error();
    }

    std::string_view message() const;

    friend constexpr bool operator==(const error& lhs, const error& rhs) noexcept = default;

    /// eventide-specific error codes:
    const static error operation_aborted;

    /// libuv error codes:
    const static error argument_list_too_long;
    const static error permission_denied;
    const static error address_already_in_use;
    const static error address_not_available;
    const static error address_family_not_supported;
    const static error resource_temporarily_unavailable;
    const static error addrinfo_address_family_not_supported;
    const static error addrinfo_temporary_failure;
    const static error addrinfo_bad_flags_value;
    const static error addrinfo_invalid_value_for_hints;
    const static error addrinfo_request_canceled;
    const static error addrinfo_permanent_failure;
    const static error addrinfo_family_not_supported;
    const static error addrinfo_out_of_memory;
    const static error addrinfo_no_address;
    const static error addrinfo_unknown_node_or_service;
    const static error addrinfo_argument_buffer_overflow;
    const static error addrinfo_resolved_protocol_unknown;
    const static error addrinfo_service_not_available_for_socket_type;
    const static error addrinfo_socket_type_not_supported;
    const static error connection_already_in_progress;
    const static error bad_file_descriptor;
    const static error resource_busy_or_locked;
    const static error invalid_unicode_character;
    const static error software_caused_connection_abort;
    const static error connection_refused;
    const static error connection_reset_by_peer;
    const static error destination_address_required;
    const static error file_already_exists;
    const static error bad_address_in_system_call_argument;
    const static error file_too_large;
    const static error host_is_unreachable;
    const static error interrupted_system_call;
    const static error invalid_argument;
    const static error io_error;
    const static error socket_is_already_connected;
    const static error illegal_operation_on_a_directory;
    const static error too_many_symbolic_links_encountered;
    const static error too_many_open_files;
    const static error message_too_long;
    const static error name_too_long;
    const static error network_is_down;
    const static error network_is_unreachable;
    const static error file_table_overflow;
    const static error no_buffer_space_available;
    const static error no_such_device;
    const static error no_such_file_or_directory;
    const static error not_enough_memory;
    const static error machine_is_not_on_the_network;
    const static error protocol_not_available;
    const static error no_space_left_on_device;
    const static error function_not_implemented;
    const static error socket_is_not_connected;
    const static error not_a_directory;
    const static error directory_not_empty;
    const static error socket_operation_on_non_socket;
    const static error operation_not_supported_on_socket;
    const static error value_too_large_for_defined_data_type;
    const static error operation_not_permitted;
    const static error broken_pipe;
    const static error protocol_error;
    const static error protocol_not_supported;
    const static error protocol_wrong_type_for_socket;
    const static error result_too_large;
    const static error read_only_file_system;
    const static error cannot_send_after_transport_endpoint_shutdown;
    const static error invalid_seek;
    const static error no_such_process;
    const static error connection_timed_out;
    const static error text_file_is_busy;
    const static error cross_device_link_not_permitted;
    const static error unknown_error;
    const static error end_of_file;
    const static error no_such_device_or_address;
    const static error too_many_links;
    const static error host_is_down;
    const static error remote_io_error;
    const static error inappropriate_ioctl_for_device;
    const static error inappropriate_file_type_or_format;
    const static error illegal_byte_sequence;
    const static error socket_type_not_supported;
    const static error no_data_available;
    const static error protocol_driver_not_attached;
    const static error exec_format_error;

private:
    int code = 0;
};

struct cancellation {
    std::string message;

    cancellation() noexcept = default;

    explicit cancellation(std::string reason) : message(std::move(reason)) {}

    std::string_view reason() const noexcept {
        return message;
    }
};

/// result<T>: value-or-error (no cancel channel). I/O functions use this.
template <typename T>
using result = outcome<T, error, void>;

}  // namespace eventide
