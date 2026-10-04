#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/vocab/error.h"

namespace kota {

namespace {

ZEST_SUITE(async_vocab_error) {

ZEST_CASE(default_is_success) {
    error e;
    ZEXPECT(!e);
    ZEXPECT(!e.has_error());
    ZEXPECT(e.value() == 0);
    ZEXPECT(e.message() == "success");
}

ZEST_CASE(named_code_is_an_error) {
    auto e = error::no_such_file_or_directory;
    ZEXPECT(static_cast<bool>(e));
    ZEXPECT(e.has_error());
    ZEXPECT(e.value() != 0);
    ZEXPECT(e == error(e.value()));
    ZEXPECT(e != error::permission_denied);
}

ZEST_CASE(message_describes_the_code) {
    ZEXPECT(error::operation_aborted.message() == "operation aborted");
    ZEXPECT(zest::contains(error::no_such_file_or_directory.message(), "no such file"));
}

ZEST_CASE(clear_resets_to_success) {
    auto e = error::io_error;
    e.clear();
    ZEXPECT(!e);
    ZEXPECT(e == error());
}

};  // ZEST_SUITE(async_vocab_error)

}  // namespace

}  // namespace kota
