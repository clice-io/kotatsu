#include <string>

#include "support/harness/throws.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/outcome.h"
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

ZEST_CASE(result_is_an_outcome_of_an_error) {
    ZEXPECT(zest::type_eq<result<int>, outcome<int, error, void>>());
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(unwrap_of_a_result_fails_with_the_errors_message) {
    result<int> failed = outcome_error(error::connection_refused);
    ZEXPECT(test::thrown<bad_outcome_access>([&] { failed.unwrap(); }) ==
            std::string(error::connection_refused.message()));
}

#endif  // KOTA_ENABLE_EXCEPTIONS

};  // ZEST_SUITE(async_vocab_error)

}  // namespace

}  // namespace kota
