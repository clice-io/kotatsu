#include "eventide/async/error.h"

#include "libuv.h"

namespace eventide {

std::string_view error::message() const {
    if(code == 0) {
        return "success";
    } else if(code == operation_aborted.value()) {
        return "operation aborted";
    }

    auto msg = uv::strerror(code);
    if(!msg.empty()) {
        return msg;
    }

    return "unknown error";
}

const error error::operation_aborted{-114514};

const error error::argument_list_too_long{UV_E2BIG};
const error error::permission_denied{UV_EACCES};
const error error::address_already_in_use{UV_EADDRINUSE};
const error error::address_not_available{UV_EADDRNOTAVAIL};
const error error::address_family_not_supported{UV_EAFNOSUPPORT};
const error error::resource_temporarily_unavailable{UV_EAGAIN};
const error error::addrinfo_address_family_not_supported{UV_EAI_ADDRFAMILY};
const error error::addrinfo_temporary_failure{UV_EAI_AGAIN};
const error error::addrinfo_bad_flags_value{UV_EAI_BADFLAGS};
const error error::addrinfo_invalid_value_for_hints{UV_EAI_BADHINTS};
const error error::addrinfo_request_canceled{UV_EAI_CANCELED};
const error error::addrinfo_permanent_failure{UV_EAI_FAIL};
const error error::addrinfo_family_not_supported{UV_EAI_FAMILY};
const error error::addrinfo_out_of_memory{UV_EAI_MEMORY};
const error error::addrinfo_no_address{UV_EAI_NODATA};
const error error::addrinfo_unknown_node_or_service{UV_EAI_NONAME};
const error error::addrinfo_argument_buffer_overflow{UV_EAI_OVERFLOW};
const error error::addrinfo_resolved_protocol_unknown{UV_EAI_PROTOCOL};
const error error::addrinfo_service_not_available_for_socket_type{UV_EAI_SERVICE};
const error error::addrinfo_socket_type_not_supported{UV_EAI_SOCKTYPE};
const error error::connection_already_in_progress{UV_EALREADY};
const error error::bad_file_descriptor{UV_EBADF};
const error error::resource_busy_or_locked{UV_EBUSY};
const error error::invalid_unicode_character{UV_ECHARSET};
const error error::software_caused_connection_abort{UV_ECONNABORTED};
const error error::connection_refused{UV_ECONNREFUSED};
const error error::connection_reset_by_peer{UV_ECONNRESET};
const error error::destination_address_required{UV_EDESTADDRREQ};
const error error::file_already_exists{UV_EEXIST};
const error error::bad_address_in_system_call_argument{UV_EFAULT};
const error error::file_too_large{UV_EFBIG};
const error error::host_is_unreachable{UV_EHOSTUNREACH};
const error error::interrupted_system_call{UV_EINTR};
const error error::invalid_argument{UV_EINVAL};
const error error::io_error{UV_EIO};
const error error::socket_is_already_connected{UV_EISCONN};
const error error::illegal_operation_on_a_directory{UV_EISDIR};
const error error::too_many_symbolic_links_encountered{UV_ELOOP};
const error error::too_many_open_files{UV_EMFILE};
const error error::message_too_long{UV_EMSGSIZE};
const error error::name_too_long{UV_ENAMETOOLONG};
const error error::network_is_down{UV_ENETDOWN};
const error error::network_is_unreachable{UV_ENETUNREACH};
const error error::file_table_overflow{UV_ENFILE};
const error error::no_buffer_space_available{UV_ENOBUFS};
const error error::no_such_device{UV_ENODEV};
const error error::no_such_file_or_directory{UV_ENOENT};
const error error::not_enough_memory{UV_ENOMEM};
const error error::machine_is_not_on_the_network{UV_ENONET};
const error error::protocol_not_available{UV_ENOPROTOOPT};
const error error::no_space_left_on_device{UV_ENOSPC};
const error error::function_not_implemented{UV_ENOSYS};
const error error::socket_is_not_connected{UV_ENOTCONN};
const error error::not_a_directory{UV_ENOTDIR};
const error error::directory_not_empty{UV_ENOTEMPTY};
const error error::socket_operation_on_non_socket{UV_ENOTSOCK};
const error error::operation_not_supported_on_socket{UV_ENOTSUP};
const error error::value_too_large_for_defined_data_type{UV_EOVERFLOW};
const error error::operation_not_permitted{UV_EPERM};
const error error::broken_pipe{UV_EPIPE};
const error error::protocol_error{UV_EPROTO};
const error error::protocol_not_supported{UV_EPROTONOSUPPORT};
const error error::protocol_wrong_type_for_socket{UV_EPROTOTYPE};
const error error::result_too_large{UV_ERANGE};
const error error::read_only_file_system{UV_EROFS};
const error error::cannot_send_after_transport_endpoint_shutdown{UV_ESHUTDOWN};
const error error::invalid_seek{UV_ESPIPE};
const error error::no_such_process{UV_ESRCH};
const error error::connection_timed_out{UV_ETIMEDOUT};
const error error::text_file_is_busy{UV_ETXTBSY};
const error error::cross_device_link_not_permitted{UV_EXDEV};
const error error::unknown_error{UV_UNKNOWN};
const error error::end_of_file{UV_EOF};
const error error::no_such_device_or_address{UV_ENXIO};
const error error::too_many_links{UV_EMLINK};
const error error::host_is_down{UV_EHOSTDOWN};
const error error::remote_io_error{UV_EREMOTEIO};
const error error::inappropriate_ioctl_for_device{UV_ENOTTY};
const error error::inappropriate_file_type_or_format{UV_EFTYPE};
const error error::illegal_byte_sequence{UV_EILSEQ};
const error error::socket_type_not_supported{UV_ESOCKTNOSUPPORT};
const error error::no_data_available{UV_ENODATA};
const error error::protocol_driver_not_attached{UV_EUNATCH};
const error error::exec_format_error{UV_ENOEXEC};

}  // namespace eventide
