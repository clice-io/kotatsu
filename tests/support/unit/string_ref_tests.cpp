#include <format>
#include <string>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/string_ref.h"

namespace kota {

namespace {

/// The parts the callback form of split() hands over.
template <typename Separator>
std::vector<std::string>
    split_all(string_ref text, Separator separator, int max_split = -1, bool keep_empty = true) {
    std::vector<std::string> parts;
    text.split([&](string_ref part) { parts.push_back(part.str()); },
               separator,
               max_split,
               keep_empty);
    return parts;
}

std::pair<std::string, std::string> strings(std::pair<string_ref, string_ref> parts) {
    return {parts.first.str(), parts.second.str()};
}

ZEST_SUITE(support_string_ref) {

ZEST_CASE(constructs_from_strings_and_views) {
    const std::string owned = "owned";
    string_ref from_string(owned);
    string_ref from_view(std::string_view("view"));
    string_ref from_pointer("pointer");
    string_ref from_part("partial", 4);
    ZEXPECT(from_string == "owned");
    ZEXPECT(from_view == "view");
    ZEXPECT(from_pointer == "pointer");
    ZEXPECT(from_part == "part");
    ZEXPECT(string_ref().empty());
}

ZEST_CASE(formats_like_a_view) {
    ZEXPECT(std::format("[{:>5}]", string_ref("abc")) == "[  abc]");
}

ZEST_CASE(compares_ignoring_ascii_case) {
    ZEXPECT(string_ref("Hello").compare_insensitive("hELLO") == 0);
    ZEXPECT(string_ref("abc").compare_insensitive("ABD") < 0);
    ZEXPECT(string_ref("abd").compare_insensitive("ABC") > 0);
    ZEXPECT(string_ref("ab").compare_insensitive("ABC") < 0);
    ZEXPECT(string_ref("abc").compare_insensitive("AB") > 0);
    ZEXPECT(string_ref("\xC3\xA9").compare_insensitive("\xC3\x89") != 0);
}

ZEST_CASE(equals_ignoring_ascii_case) {
    ZEXPECT(string_ref("Kotatsu").equals_insensitive("kOTATSU"));
    ZEXPECT(!string_ref("Kotatsu").equals_insensitive("Kotatsu!"));
}

ZEST_CASE(prefixes_and_suffixes_ignoring_case) {
    string_ref text("Header.H");
    ZEXPECT(text.starts_with_insensitive("HEAD"));
    ZEXPECT(!text.starts_with_insensitive("header.h.x"));
    ZEXPECT(text.ends_with_insensitive(".h"));
    ZEXPECT(!text.ends_with_insensitive(".c"));
    ZEXPECT(!string_ref("h").ends_with_insensitive(".h"));
}

ZEST_CASE(contains_insensitive_ignores_ascii_case) {
    string_ref text("needle in haystack");
    ZEXPECT(text.contains_insensitive("HAYSTACK"));
    ZEXPECT(text.contains_insensitive("In Hay"));
    ZEXPECT(!text.contains_insensitive("HAYSTACKS"));
}

ZEST_CASE(find_by_predicate) {
    string_ref text("ab1c2");
    auto digit = [](char c) {
        return c >= '0' && c <= '9';
    };
    ZEXPECT(text.find_if(digit) == 2U);
    ZEXPECT(text.find_if(digit, 3) == 4U);
    ZEXPECT(text.find_if(digit, 100) == string_ref::npos);
    ZEXPECT(text.find_if_not(digit, 2) == 3U);
    ZEXPECT(string_ref("123").find_if_not(digit) == string_ref::npos);
}

ZEST_CASE(find_ignoring_case) {
    string_ref text("aXbxC");
    ZEXPECT(text.find_insensitive('x') == 1U);
    ZEXPECT(text.find_insensitive('X', 2) == 3U);
    ZEXPECT(text.find_insensitive('z') == string_ref::npos);
    ZEXPECT(text.find_insensitive("BXc") == 2U);
    ZEXPECT(text.find_insensitive("xb", 2) == string_ref::npos);
    ZEXPECT(text.find_insensitive("") == 0U);
}

ZEST_CASE(rfind_ignoring_case) {
    string_ref text("aXbxC");
    ZEXPECT(text.rfind_insensitive('X') == 3U);
    ZEXPECT(text.rfind_insensitive('x', 2) == 1U);
    ZEXPECT(text.rfind_insensitive('z') == string_ref::npos);
    ZEXPECT(string_ref().rfind_insensitive('a') == string_ref::npos);
    ZEXPECT(text.rfind_insensitive("XB") == 1U);
    ZEXPECT(text.rfind_insensitive("xc") == 3U);
    ZEXPECT(text.rfind_insensitive("longer than the text") == string_ref::npos);
}

ZEST_CASE(count_characters_and_strings) {
    string_ref text("abababa");
    ZEXPECT(text.count('a') == 4U);
    ZEXPECT(text.count('z') == 0U);
    // Non-overlapping occurrences.
    ZEXPECT(text.count("aba") == 2U);
    ZEXPECT(text.count("") == 0U);
}

ZEST_CASE(substr_and_slice_clamp_to_the_text) {
    string_ref text("hello");
    ZEXPECT(text.substr(1, 3) == "ell");
    ZEXPECT(text.substr(3) == "lo");
    ZEXPECT(text.substr(10).empty());
    ZEXPECT(text.slice(1, 4) == "ell");
    ZEXPECT(text.slice(3, 1).empty());
    ZEXPECT(text.slice(2, 100) == "llo");
}

ZEST_CASE(take_and_drop_clamp_to_the_text) {
    string_ref text("hello");
    ZEXPECT(text.take_front(2) == "he");
    ZEXPECT(text.take_front(10) == "hello");
    ZEXPECT(text.take_back(2) == "lo");
    ZEXPECT(text.take_back(10) == "hello");
    ZEXPECT(text.drop_front(2) == "llo");
    ZEXPECT(text.drop_back(2) == "hel");
    ZEXPECT(text.drop_front() == "ello");
    ZEXPECT(text.drop_back() == "hell");
}

ZEST_CASE(take_and_drop_by_predicate) {
    string_ref text("  indented");
    auto space = [](char c) {
        return c == ' ';
    };
    ZEXPECT(text.take_while(space) == "  ");
    ZEXPECT(text.drop_while(space) == "indented");
    ZEXPECT(text.take_until([](char c) { return c == 'd'; }) == "  in");
    ZEXPECT(text.drop_until([](char c) { return c == 'd'; }) == "dented");
}

ZEST_CASE(consume_affixes) {
    string_ref text("--flag=value");
    ZEXPECT(text.consume_front("--"));
    ZEXPECT(text == "flag=value");
    ZEXPECT(!text.consume_front("--"));
    ZEXPECT(text.consume_back("=value"));
    ZEXPECT(text == "flag");
    ZEXPECT(!text.consume_back("x"));
    ZEXPECT(text == "flag");
}

ZEST_CASE(consume_affixes_ignoring_case) {
    string_ref text("HTTP://Host.COM");
    ZEXPECT(text.consume_front_insensitive("http://"));
    ZEXPECT(text.consume_back_insensitive(".com"));
    ZEXPECT(text == "Host");
    ZEXPECT(!text.consume_front_insensitive("x"));
    ZEXPECT(!text.consume_back_insensitive("x"));
}

ZEST_CASE(trim_removes_a_character_from_either_end) {
    string_ref text("xxmiddlexx");
    ZEXPECT(text.ltrim('x') == "middlexx");
    ZEXPECT(text.rtrim('x') == "xxmiddle");
    ZEXPECT(text.trim('x') == "middle");
    ZEXPECT(string_ref("xxx").trim('x').empty());
}

ZEST_CASE(trim_character_sets) {
    string_ref text(" \t middle \r\n");
    ZEXPECT(text.ltrim() == "middle \r\n");
    ZEXPECT(text.rtrim() == " \t middle");
    ZEXPECT(text.trim() == "middle");
    ZEXPECT(string_ref("-+x+-").trim("+-") == "x");
    ZEXPECT(string_ref(" \n").trim().empty());
}

ZEST_CASE(split_at_the_first_separator) {
    string_ref text("key=value=more");
    ZEXPECT(strings(text.split('=')) == std::pair<std::string, std::string>{"key", "value=more"});
    ZEXPECT(strings(text.split("=v")) == std::pair<std::string, std::string>{"key", "alue=more"});
    ZEXPECT(strings(text.split(':')) == std::pair<std::string, std::string>{"key=value=more", ""});
    ZEXPECT(strings(text.split("::")) == std::pair<std::string, std::string>{"key=value=more", ""});
}

ZEST_CASE(split_at_the_last_separator) {
    string_ref text("a/b/c");
    ZEXPECT(strings(text.rsplit('/')) == std::pair<std::string, std::string>{"a/b", "c"});
    ZEXPECT(strings(text.rsplit("/b")) == std::pair<std::string, std::string>{"a", "/c"});
    ZEXPECT(strings(text.rsplit('.')) == std::pair<std::string, std::string>{"a/b/c", ""});
    ZEXPECT(strings(text.rsplit("..")) == std::pair<std::string, std::string>{"a/b/c", ""});
}

ZEST_CASE(split_into_parts_by_character) {
    ZEXPECT(split_all("a,b,,c", ',') == std::vector<std::string>{"a", "b", "", "c"});
    ZEXPECT(split_all("a,b,,c", ',', -1, false) == std::vector<std::string>{"a", "b", "c"});
    ZEXPECT(split_all("a,b,c", ',', 1) == std::vector<std::string>{"a", "b,c"});
    ZEXPECT(split_all("a,b,c", ',', 0) == std::vector<std::string>{"a,b,c"});
    ZEXPECT(split_all(",", ',', -1, false).empty());
}

ZEST_CASE(split_into_parts_by_string) {
    ZEXPECT(split_all("a::b::::c", "::") == std::vector<std::string>{"a", "b", "", "c"});
    ZEXPECT(split_all("a::b::::c", "::", -1, false) == std::vector<std::string>{"a", "b", "c"});
    ZEXPECT(split_all("a::b::c", "::", 1) == std::vector<std::string>{"a", "b::c"});
    // An empty separator hands over the whole text.
    ZEXPECT(split_all("abc", "") == std::vector<std::string>{"abc"});
}

ZEST_CASE(change_ascii_case) {
    string_ref text("MiXeD 123 \xC3\xA9");
    ZEXPECT(text.lower() == "mixed 123 \xC3\xA9");
    ZEXPECT(text.upper() == "MIXED 123 \xC3\xA9");
    ZEXPECT(text.str() == "MiXeD 123 \xC3\xA9");
}

ZEST_CASE(keeps_the_overloads_of_string_view) {
    string_ref text("path/to/  ");
    ZEXPECT(text.find_last_not_of(" /") == 6U);
    ZEXPECT(text.find_last_not_of(' ') == 7U);
    ZEXPECT(text.compare(0, 4, "path") == 0);
    ZEXPECT(text.compare("path") > 0);
}

};  // ZEST_SUITE(support_string_ref)

}  // namespace

}  // namespace kota
