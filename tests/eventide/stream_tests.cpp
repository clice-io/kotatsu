#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "eventide/zest/macro.h"
#include "eventide/async/error.h"

#ifdef _WIN32
#include <BaseTsd.h>
#include <fcntl.h>
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/stream.h"
#include "eventide/async/watcher.h"

namespace eventide {

#ifdef _WIN32
struct wsa_init_guard {
    wsa_init_guard() {
        WSADATA data{};
        (void)WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~wsa_init_guard() {
        WSACleanup();
    }
};

static wsa_init_guard wsa_guard;
using ssize_t = SSIZE_T;
#endif

namespace {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
constexpr int socket_error = SOCKET_ERROR;

inline int close_socket(socket_t sock) {
    return ::closesocket(sock);
}

inline int close_fd(int fd) {
    return _close(fd);
}

inline ssize_t write_fd(int fd, const char* data, size_t len) {
    return _write(fd, data, static_cast<unsigned int>(len));
}

inline int create_pipe(int fds[2]) {
    return _pipe(fds, 4096, _O_BINARY);
}
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;
constexpr int socket_error = -1;

inline int close_socket(socket_t sock) {
    return ::close(sock);
}

inline int close_fd(int fd) {
    return ::close(fd);
}

inline ssize_t write_fd(int fd, const char* data, size_t len) {
    return ::write(fd, data, len);
}

inline int create_pipe(int fds[2]) {
    return ::pipe(fds);
}
#endif

int pick_free_port() {
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(fd == invalid_socket) {
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(fd);
        return -1;
    }

    socklen_t len = sizeof(addr);
    if(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        close_socket(fd);
        return -1;
    }

    int port = ntohs(addr.sin_port);
    close_socket(fd);
    return port;
}

bool bump_and_stop(int& done, int target) {
    done += 1;
    if(done == target) {
        event_loop::current().stop();
        return true;
    }
    return false;
}

task<result<std::string>> read_from_pipe(pipe p) {
    auto out = co_await p.read();
    event_loop::current().stop();
    co_return out;
}

task<result<std::string>> read_some_from_pipe(pipe p) {
    std::array<char, 64> buf{};
    auto n = co_await p.read_some(std::span<char>(buf.data(), buf.size()));
    event_loop::current().stop();
    if(!n.has_value()) {
        co_return std::unexpected(n.error());
    }
    co_return std::string(buf.data(), *n);
}

task<std::pair<std::string, std::size_t>> read_chunk_from_pipe(pipe p) {
    auto view = co_await p.read_chunk();
    if(!view) {
        event_loop::current().stop();
        co_return std::make_pair(std::string{}, static_cast<std::size_t>(0));
    }
    std::string out(view->begin(), view->end());
    p.consume(view->size());

    auto next = co_await p.read_chunk();
    event_loop::current().stop();

    if(!next) {
        co_return std::make_pair(out, static_cast<std::size_t>(0));
    }
    co_return std::make_pair(out, next->size());
}

task<std::pair<result<std::string>, result<std::string>>> read_chunk_then_some(pipe p) {
    auto first = co_await p.read_chunk();
    result<std::string> first_out = std::unexpected(error::invalid_argument);
    if(first) {
        first_out = std::string(first->data(), first->size());
        p.consume(first->size());
    } else {
        first_out = std::unexpected(first.error());
    }

    std::array<char, 64> buf{};
    auto second = co_await p.read_some(std::span<char>(buf.data(), buf.size()));
    result<std::string> second_out = std::unexpected(error::invalid_argument);
    if(second) {
        second_out = std::string(buf.data(), *second);
    } else {
        second_out = std::unexpected(second.error());
    }

    event_loop::current().stop();
    co_return std::pair{std::move(first_out), std::move(second_out)};
}

task<> write_two_pipe_chunks(int fd, event_loop& loop) {
    constexpr std::string_view first = "eventide-chunk";
    constexpr std::string_view second = "eventide-read-some";

    co_await sleep(std::chrono::milliseconds{1}, loop);
    if(write_fd(fd, first.data(), first.size()) != static_cast<ssize_t>(first.size())) {
        close_fd(fd);
        co_return;
    }

    co_await sleep(std::chrono::milliseconds{1}, loop);
    (void)write_fd(fd, second.data(), second.size());
    close_fd(fd);
}

task<result<pipe>> connect_pipe(std::string_view name, int& done, int target = 2) {
    auto result = co_await pipe::connect(name);
    bump_and_stop(done, target);
    co_return result;
}

task<result<pipe>> accept_pipe_once(pipe::acceptor acc, int& done) {
    auto result = co_await acc.accept();
    bump_and_stop(done, 2);
    co_return result;
}

task<result<std::string>> accept_and_read_pipe(pipe::acceptor acc, int& done) {
    auto conn_res = co_await acc.accept();
    if(!conn_res.has_value()) {
        bump_and_stop(done, 2);
        co_return std::unexpected(conn_res.error());
    }

    auto conn = std::move(*conn_res);
    auto data = co_await conn.read();

    bump_and_stop(done, 2);
    co_return data;
}

task<error> connect_and_write_pipe(std::string_view name, std::string_view payload, int& done) {
    auto conn_res = co_await pipe::connect(name);
    if(!conn_res.has_value()) {
        bump_and_stop(done, 2);
        co_return conn_res.error();
    }

    auto conn = std::move(*conn_res);
    std::span<const char> data(payload.data(), payload.size());
    auto err = co_await conn.write(data);

    bump_and_stop(done, 2);
    co_return err;
}

task<result<std::string>> accept_and_read(tcp_socket::acceptor acc) {
    auto conn_res = co_await acc.accept();
    if(!conn_res.has_value()) {
        event_loop::current().stop();
        co_return std::unexpected(conn_res.error());
    }

    auto conn = std::move(*conn_res);
    auto data = co_await conn.read();

    event_loop::current().stop();
    co_return data;
}

task<result<std::size_t>> accept_and_read_some(tcp_socket::acceptor acc) {
    auto conn_res = co_await acc.accept();
    if(!conn_res.has_value()) {
        event_loop::current().stop();
        co_return std::unexpected(conn_res.error());
    }

    auto conn = std::move(*conn_res);
    std::array<char, 64> buf{};
    auto data = co_await conn.read_some(std::span<char>(buf.data(), buf.size()));

    event_loop::current().stop();
    co_return data;
}

task<result<std::string>> accept_and_read_once(tcp_socket::acceptor acc, int& done) {
    auto conn_res = co_await acc.accept();
    if(!conn_res.has_value()) {
        bump_and_stop(done, 2);
        co_return std::unexpected(conn_res.error());
    }

    auto conn = std::move(*conn_res);
    auto data = co_await conn.read();

    bump_and_stop(done, 2);
    co_return data;
}

task<error> connect_and_send(std::string_view host, int port, std::string_view payload, int& done) {
    auto conn_res = co_await tcp_socket::connect(host, port);
    if(!conn_res.has_value()) {
        bump_and_stop(done, 2);
        co_return conn_res.error();
    }

    auto conn = std::move(*conn_res);
    std::span<const char> data(payload.data(), payload.size());
    auto err = co_await conn.write(data);

    bump_and_stop(done, 2);
    co_return err;
}

task<result<tcp_socket>> accept_once(tcp_socket::acceptor& acc, int& done) {
    auto res = co_await acc.accept();
    bump_and_stop(done, 2);
    co_return res;
}

int set_abortive_close(socket_t sock) {
    linger opt{};
    opt.l_onoff = 1;
    opt.l_linger = 0;
#ifdef _WIN32
    return ::setsockopt(sock,
                        SOL_SOCKET,
                        SO_LINGER,
                        reinterpret_cast<const char*>(&opt),
                        static_cast<int>(sizeof(opt)));
#else
    return ::setsockopt(sock, SOL_SOCKET, SO_LINGER, &opt, sizeof(opt));
#endif
}

}  // namespace

TEST_SUITE(pipe) {

TEST_CASE(read_from_fd) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    const std::string message = "eventide-pipe";
    ASSERT_EQ(write_fd(fds[1], message.data(), message.size()),
              static_cast<ssize_t>(message.size()));
    close_fd(fds[1]);

    event_loop loop;
    auto pipe_res = pipe::open(fds[0], {}, loop);
    ASSERT_TRUE(pipe_res.has_value());

    auto reader = read_from_pipe(std::move(*pipe_res));

    loop.schedule(reader);
    loop.run();

    auto result = reader.result();
    EXPECT_TRUE(result.has_value());
    if(result.has_value()) {
        EXPECT_EQ(*result, message);
    }
}

TEST_CASE(read_some_fd) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    const std::string message = "eventide-pipe-read-some";
    ASSERT_EQ(write_fd(fds[1], message.data(), message.size()),
              static_cast<ssize_t>(message.size()));
    close_fd(fds[1]);

    event_loop loop;
    auto pipe_res = pipe::open(fds[0], {}, loop);
    ASSERT_TRUE(pipe_res.has_value());

    auto reader = read_some_from_pipe(std::move(*pipe_res));

    loop.schedule(reader);
    loop.run();

    auto result = reader.result();
    EXPECT_TRUE(result.has_value());
    if(result.has_value()) {
        EXPECT_EQ(*result, message);
    }
}

TEST_CASE(read_chunk_fd) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    const std::string message = "eventide-pipe-read-view";
    ASSERT_EQ(write_fd(fds[1], message.data(), message.size()),
              static_cast<ssize_t>(message.size()));
    close_fd(fds[1]);

    event_loop loop;
    auto pipe_res = pipe::open(fds[0], {}, loop);
    ASSERT_TRUE(pipe_res.has_value());

    auto reader = read_chunk_from_pipe(std::move(*pipe_res));

    loop.schedule(reader);
    loop.run();

    auto result = reader.result();
    EXPECT_EQ(result.first, message);
    EXPECT_EQ(result.second, static_cast<std::size_t>(0));
}

TEST_CASE(read_chunk_then_read_some_fd) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(create_pipe(fds), 0);

    event_loop loop;
    auto pipe_res = pipe::open(fds[0], {}, loop);
    ASSERT_TRUE(pipe_res.has_value());

    auto reader = read_chunk_then_some(std::move(*pipe_res));
    auto writer = write_two_pipe_chunks(fds[1], loop);

    loop.schedule(reader);
    loop.schedule(writer);
    loop.run();

    auto [first, second] = reader.result();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, "eventide-chunk");
    EXPECT_EQ(*second, "eventide-read-some");
}

TEST_CASE(connect_and_accept) {
    event_loop loop;

#ifdef _WIN32
    const std::string name = "\\\\.\\pipe\\eventide-test-pipe";
#else
    std::string name = "eventide-test-pipe-XXXXXX";
    int fd = ::mkstemp(name.data());
    ASSERT_TRUE(fd >= 0);
    close_fd(fd);
    ::unlink(name.c_str());
#endif

    pipe::options opts{};
    opts.backlog = 16;
    auto acc_res = pipe::listen(name, opts, loop);
    ASSERT_TRUE(acc_res.has_value());

    int done = 0;
    const std::string message = "eventide-pipe-connect";
    auto server = accept_and_read_pipe(std::move(*acc_res), done);
    auto client = connect_and_write_pipe(name, message, done);

    loop.schedule(server);
    loop.schedule(client);
    loop.run();

    auto server_res = server.result();
    auto client_res = client.result();

    EXPECT_TRUE(server_res.has_value());
    EXPECT_FALSE(client_res.has_error());
    if(server_res.has_value()) {
        EXPECT_EQ(*server_res, message);
    }
}

TEST_CASE(connect_failure) {
    event_loop loop;

#ifdef _WIN32
    const std::string name = "\\\\.\\pipe\\eventide-test-pipe-missing";
#else
    std::string name = "eventide-test-pipe-missing-XXXXXX";
    int fd = ::mkstemp(name.data());
    ASSERT_TRUE(fd >= 0);
    close_fd(fd);
    ::unlink(name.c_str());
#endif

    int done = 0;
    auto client = connect_pipe(name, done, 1);

    loop.schedule(client);
    loop.run();

    auto client_res = client.result();
    EXPECT_FALSE(client_res.has_value());
}

TEST_CASE(stop) {
    event_loop loop;

#ifdef _WIN32
    const std::string name = "\\\\.\\pipe\\eventide-test-pipe-missing";
#else
    std::string name = "eventide-test-pipe-missing-XXXXXX";
    int fd = ::mkstemp(name.data());
    ASSERT_TRUE(fd >= 0);
    close_fd(fd);
    ::unlink(name.c_str());
#endif

    pipe::options opts{};
    opts.backlog = 16;
    auto acc = pipe::listen(name, opts, loop);
    ASSERT_TRUE(acc.has_value());

    auto err = acc->stop();
    EXPECT_FALSE(err.has_error());

    auto task1 = [](acceptor<pipe>& acc) -> eventide::task<result<pipe>> {
        auto res = co_await acc.accept();
        event_loop::current().stop();
        co_return res;
    }(*acc);

    loop.schedule(task1);
    loop.run();

    auto res1 = task1.value().value();
    EXPECT_TRUE(!res1.has_value() && res1.error() == error::operation_aborted);

    auto task2 = [](acceptor<pipe>& acc) -> eventide::task<result<pipe>> {
        event_loop::current().stop();
        auto res = co_await acc.accept();
        co_return res;
    }(*acc);

    loop.schedule(task2);
    loop.run();

    EXPECT_TRUE(!task2->is_finished());
    acc->stop();
    EXPECT_TRUE(task2->is_finished());
}

};  // TEST_SUITE(pipe)

TEST_SUITE(tcp) {

TEST_CASE(accept_and_read) {
    int port = pick_free_port();
    ASSERT_TRUE(port > 0);

    event_loop loop;
    auto acc_res = tcp_socket::listen("127.0.0.1", port, {}, loop);
    ASSERT_TRUE(acc_res.has_value());

    auto server = accept_and_read(std::move(*acc_res));

    socket_t client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_TRUE(client_fd != invalid_socket);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

    const std::string message = "eventide-tcp";
    ASSERT_EQ(::send(client_fd, message.data(), message.size(), 0),
              static_cast<ssize_t>(message.size()));
    close_socket(client_fd);

    loop.schedule(server);
    loop.run();

    auto result = server.result();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, message);
}

TEST_CASE(accept_already_waiting) {
    int port = pick_free_port();
    ASSERT_TRUE(port > 0);

    event_loop loop;
    auto acc_res = tcp_socket::listen("127.0.0.1", port, {}, loop);
    ASSERT_TRUE(acc_res.has_value());

    auto acc = std::move(*acc_res);
    int done = 0;

    auto first = accept_once(acc, done);
    auto second = accept_once(acc, done);

    socket_t client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_TRUE(client_fd != invalid_socket);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    close_socket(client_fd);

    loop.schedule(first);
    loop.schedule(second);
    loop.run();

    auto first_res = first.result();
    auto second_res = second.result();
    EXPECT_TRUE(first_res.has_value());
    EXPECT_FALSE(second_res.has_value());
    if(!second_res.has_value()) {
        EXPECT_EQ(second_res.error().value(), error::connection_already_in_progress.value());
    }
}

TEST_CASE(connect_and_write) {
    int port = pick_free_port();
    ASSERT_TRUE(port > 0);

    event_loop loop;
    auto acc_res = tcp_socket::listen("127.0.0.1", port, {}, loop);
    ASSERT_TRUE(acc_res.has_value());

    int done = 0;
    auto server = accept_and_read_once(std::move(*acc_res), done);
    auto client = connect_and_send("127.0.0.1", port, "eventide-tcp-connect", done);

    loop.schedule(server);
    loop.schedule(client);
    loop.run();

    auto server_res = server.result();
    auto client_res = client.result();
    EXPECT_TRUE(server_res.has_value());
    EXPECT_EQ(*server_res, "eventide-tcp-connect");
    EXPECT_FALSE(static_cast<bool>(client_res));
}

TEST_CASE(read_some_error) {
    int port = pick_free_port();
    ASSERT_TRUE(port > 0);

    event_loop loop;
    auto acc_res = tcp_socket::listen("127.0.0.1", port, {}, loop);
    ASSERT_TRUE(acc_res.has_value());

    auto server = accept_and_read_some(std::move(*acc_res));

    socket_t client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_TRUE(client_fd != invalid_socket);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(set_abortive_close(client_fd), 0);
    close_socket(client_fd);

    loop.schedule(server);
    loop.run();

    auto result = server.result();
    EXPECT_FALSE(result.has_value());
}

};  // TEST_SUITE(tcp)

}  // namespace eventide
