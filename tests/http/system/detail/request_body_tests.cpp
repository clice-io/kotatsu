#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "async/harness/os.h"
#include "http/harness/server.h"
#include "kota/http/http.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota::http {

namespace {

/// Makes the process's stdin a pipe that holds `text` and ends after it,
/// until this goes. curl's own reader would read it into an upload; zest's
/// workers run with a stdin that holds nothing.
class StdinHolding {
public:
    explicit StdinHolding(std::string_view text) {
        int fds[2] = {-1, -1};
        if(test::create_pipe(fds) != 0) {
            return;
        }
        // Small enough for any pipe's buffer.
        test::write_fd(fds[1], text.data(), text.size());
        test::close_fd(fds[1]);
        saved = duplicate(0);
        if(saved >= 0) {
            duplicate_onto(fds[0], 0);
            std::clearerr(stdin);
        }
        test::close_fd(fds[0]);
    }

    ~StdinHolding() {
        if(saved >= 0) {
            duplicate_onto(saved, 0);
            test::close_fd(saved);
            std::clearerr(stdin);
        }
    }

    StdinHolding(const StdinHolding&) = delete;
    StdinHolding& operator=(const StdinHolding&) = delete;

    bool holding() const noexcept {
        return saved >= 0;
    }

private:
#ifdef _WIN32
    static int duplicate(int fd) {
        return ::_dup(fd);
    }

    static int duplicate_onto(int fd, int onto) {
        return ::_dup2(fd, onto);
    }
#else
    static int duplicate(int fd) {
        return ::dup(fd);
    }

    static int duplicate_onto(int fd, int onto) {
        return ::dup2(fd, onto);
    }
#endif

    int saved = -1;
};

ZEST_SUITE(http_detail_request_body, zest::LoopFixture) {

// Without a body of its own, curl would read one, chunked, with its own
// reader, which reads the process's stdin.
ZEST_CASE(post_without_a_body_sends_an_empty_one) {
    StdinHolding input("from-stdin");
    ASSERT(input.holding());
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).post(server.url("/")).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    const auto& sent = server.requests()[0];
    EXPECT(sent.method == "POST");
    EXPECT(sent.header("content-length") == "0");
    EXPECT(!sent.chunked);
    EXPECT(sent.body.empty());
}

ZEST_CASE(empty_form_sends_an_empty_body) {
    StdinHolding input("from-stdin");
    ASSERT(input.holding());
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).post(server.url("/")).form({}).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-length") == "0");
    EXPECT(server.requests()[0].body.empty());
}

// A curl_option() may ask for an upload with no CURLOPT_READDATA: it reads
// nothing, where curl's own reader would read the process's stdin.
ZEST_CASE(upload_without_a_file_sends_nothing) {
    StdinHolding input("from-stdin");
    ASSERT(input.holding());
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).put(server.url("/")).curl_option(CURLOPT_UPLOAD, 1L).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].chunked);
    EXPECT(server.requests()[0].body.empty());
}

ZEST_CASE(upload_of_a_file_given_to_curl_sends_it) {
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::tmpfile(), std::fclose);
    ASSERT((file != nullptr));
    std::fputs("from-file", file.get());
    std::rewind(file.get());
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop)
                           .put(server.url("/"))
                           .curl_option(CURLOPT_UPLOAD, 1L)
                           .curl_option(CURLOPT_READDATA, static_cast<void*>(file.get()))
                           .send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].body == "from-file");
}

ZEST_CASE(post_fields_given_to_curl_are_sent) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] =
        run(client.on(loop).post(server.url("/")).curl_option(CURLOPT_POSTFIELDS, "a=1").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].body == "a=1");
}

ZEST_CASE(body_goes_with_its_length) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).post(server.url("/")).body("hello").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-length") == "5");
    EXPECT(server.requests()[0].body == "hello");
}

ZEST_CASE(put_patch_and_delete_send_their_bodies) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    auto api = client.on(loop);
    auto url = server.url("/");

    const std::vector<std::pair<std::string, std::string>> sends{
        {"PUT",    "put"   },
        {"PATCH",  "patch" },
        {"DELETE", "delete"},
    };
    for(const auto& [method, body]: sends) {
        ZEST_CONTEXT("{}", method);
        auto [reply] = run(api.request(method, url).body(body).send());
        EXPECT(reply.has_value());
    }

    ASSERT(server.requests().size() == 3U);
    EXPECT(server.requests()[0].method == "PUT");
    EXPECT(server.requests()[0].body == "put");
    EXPECT(server.requests()[1].method == "PATCH");
    EXPECT(server.requests()[1].body == "patch");
    EXPECT(server.requests()[2].method == "DELETE");
    EXPECT(server.requests()[2].body == "delete");
}

ZEST_CASE(body_keeps_every_byte) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    std::string bytes;
    for(int i = 0; i < 256; ++i) {
        bytes.push_back(static_cast<char>(i));
    }

    auto [reply] = run(client.on(loop).post(server.url("/")).body(bytes).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].body == bytes);
}

// Past 1 MiB curl asks whether to go on (Expect: 100-continue) before it
// sends the body.
ZEST_CASE(large_body_arrives_whole) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();
    std::string large(3 << 20, 'x');
    large.back() = 'y';

    auto [reply] = run(client.on(loop).post(server.url("/")).body(large).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].body.size() == large.size());
    // Compared as a plain bool, so that a failure does not print 3 MiB.
    EXPECT((server.requests()[0].body == large));
}

ZEST_CASE(json_text_is_sent_as_json) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop).post(server.url("/")).json_text(R"({"a":1})").send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-type") == "application/json");
    EXPECT(server.requests()[0].body == R"({"a":1})");
}

#if KOTA_HTTP_HAS_CODEC_JSON
ZEST_CASE(json_sends_its_value_encoded) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] =
        run(client.on(loop).post(server.url("/")).json(std::vector<int>{1, 2, 3}).send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-type") == "application/json");
    EXPECT(server.requests()[0].body == "[1,2,3]");
}
#endif

ZEST_CASE(form_is_sent_percent_encoded) {
    test::HttpServer server(loop);
    ASSERT(server.listening());
    auto client = test::loopback_client();

    auto [reply] = run(client.on(loop)
                           .post(server.url("/"))
                           .form({
                               {"name", "alice"},
                               {"note", "a b+c"}
    })
                           .send());
    EXPECT(reply.has_value());

    ASSERT(server.requests().size() == 1U);
    EXPECT(server.requests()[0].header("content-type") == "application/x-www-form-urlencoded");
    EXPECT(server.requests()[0].body == "name=alice&note=a%20b%2Bc");
}

};  // ZEST_SUITE(http_detail_request_body)

}  // namespace

}  // namespace kota::http
