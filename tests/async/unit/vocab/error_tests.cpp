#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/vocab/error.h"

namespace kota {

namespace {

ZEST_SUITE(async_vocab_error) {

ZEST_CASE(default_is_success) {
    error e;
    EXPECT(!e);
    EXPECT(!e.has_error());
    EXPECT(e.value() == 0);
    EXPECT(e.message() == "success");
}

ZEST_CASE(named_code_is_an_error) {
    auto e = error::no_such_file_or_directory;
    EXPECT(static_cast<bool>(e));
    EXPECT(e.has_error());
    EXPECT(e.value() != 0);
    EXPECT(e == error(e.value()));
    EXPECT(e != error::permission_denied);
}

ZEST_CASE(message_describes_the_code) {
    EXPECT(error::operation_aborted.message() == "operation aborted");
    EXPECT(zest::contains(error::no_such_file_or_directory.message(), "no such file"));
}

ZEST_CASE(clear_resets_to_success) {
    auto e = error::io_error;
    e.clear();
    EXPECT(!e);
    EXPECT(e == error());
}

ZEST_CASE(cancellation_carries_its_reason) {
    cancellation plain;
    EXPECT(plain.reason().empty());

    cancellation why("deadline passed");
    EXPECT(why.reason() == "deadline passed");
}

};  // ZEST_SUITE(async_vocab_error)

}  // namespace

}  // namespace kota
