// What parse() offers beyond the fields themselves: aliases, a visit of the arguments in
// order, the help option's stop, and values of types of their own through meta::repr.

#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "cli/harness/argv.h"
#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Optimize {
    int level = 0;
    KOTATSU_ANNOTATE(cli::names = {"-O2"},
                     cli::forward = {"--level", "2"},
                     description = "--level 2")
    <cli::Alias> o2;
};

struct Strict {
    unsigned level = 0;
    KOTATSU_ANNOTATE(cli::names = {"-Oneg"}, cli::forward = {"--level", "-1"})
    <cli::Alias> neg;
};

struct Session {
    std::vector<std::string> handler;
    std::vector<std::string> connect;
};

struct Handlers {
    Session session;
};

struct Serve {
    int port = 0;
};

struct Hub {
    std::string name;
    std::variant<Serve> command;
};

/// A port, which reads only from a number in its range.
struct Port {
    int number = 0;
};

struct Server {
    Port port;
    cli::Help help;
    KOTATSU_ANNOTATE(cli::required = true)
    <std::string> host;
};

/// What a visit sees of `given`: "session.handler=a", its values joined by ','.
std::string seen(const Given& given) {
    std::string text = given.path + "=";
    for(std::size_t i = 0; i < given.values.size(); ++i) {
        text += i == 0 ? "" : ",";
        text += given.values[i];
    }
    return text;
}

template <typename T>
std::expected<T, Error> parse_line(std::string_view line) {
    return cli::parse<T>(line.empty() ? std::vector<std::string>{} : test::split(line));
}

}  // namespace

}  // namespace kotatsu::cli

template <>
struct kotatsu::meta::repr<kotatsu::cli::Port> {
    using type = std::string;

    template <typename Config>
    static bool deserialize(auto& reader, kotatsu::cli::Port& port) {
        std::string text;
        if(!reader.visit_str(text)) {
            return false;
        }
        if(text.empty() || text.size() > 5 ||
           text.find_first_not_of("0123456789") != std::string::npos) {
            return codec::scoped_context<codec::rich_error>::fail(
                codec::rich_error("not a port: " + text));
        }
        port.number = std::stoi(text);
        return true;
    }

    template <typename Config>
    static bool serialize(auto& writer, const kotatsu::cli::Port& port) {
        return writer.visit_str(std::to_string(port.number));
    }
};

namespace kotatsu::cli {

namespace {

ZEST_SUITE(cli_parse_extension) {

ZEST_CASE(alias_reads_as_what_it_stands_for) {
    auto options = parse_line<Optimize>("-O2");
    ZASSERT(options.has_value());
    ZEXPECT(options->level == 2);
}

ZEST_CASE(alias_standing_for_a_bad_value_fails_at_the_alias) {
    auto options = parse_line<Strict>("--level 1 -Oneg");
    ZASSERT(!options.has_value());
    ZEXPECT(options.error().kind == Error::Kind::InvalidValue);
    ZEXPECT(options.error().message == "invalid integer value: -1");
    ZEXPECT(options.error().begin == 2U);
    ZEXPECT(options.error().end == 3U);
}

ZEST_CASE(visit_sees_the_arguments_in_order) {
    std::vector<std::string> order;
    auto options =
        cli::parse<Handlers>(test::split("--handler a --connect b --handler c"),
                             [&](Handlers&, const Given& given) { order.push_back(seen(given)); });
    ZASSERT(options.has_value());
    ZEXPECT(
        order ==
        (std::vector<std::string>{"session.handler=a", "session.connect=b", "session.handler=c"}));
}

ZEST_CASE(visit_sees_a_subcommand_s_arguments_under_it) {
    std::vector<std::string> order;
    auto options = cli::parse<Hub>(test::split("--name n serve --port 8"),
                                   [&](Hub&, const Given& given) { order.push_back(seen(given)); });
    ZASSERT(options.has_value());
    ZEXPECT(order == (std::vector<std::string>{"name=n", "command=serve", "command.port=8"}));
}

ZEST_CASE(help_stops_before_the_required_are_checked) {
    auto server = parse_line<Server>("--help");
    ZASSERT(server.has_value());
    ZEXPECT(server->help);
}

ZEST_CASE(repr_reads_a_value_of_its_own) {
    auto server = parse_line<Server>("--host h --port 80");
    ZASSERT(server.has_value());
    ZEXPECT(server->port.number == 80);
}

ZEST_CASE(repr_refusing_a_value_fails_at_it) {
    auto server = parse_line<Server>("--host h --port eighty");
    ZASSERT(!server.has_value());
    ZEXPECT(server.error().kind == Error::Kind::InvalidValue);
    ZEXPECT(server.error().message == "not a port: eighty");
    ZEXPECT(server.error().begin == 3U);
}

};  // ZEST_SUITE(cli_parse_extension)

}  // namespace

}  // namespace kotatsu::cli
