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
    EXPECT(from_string == "owned");
    EXPECT(from_view == "view");
    EXPECT(from_pointer == "pointer");
    EXPECT(from_part == "part");
    EXPECT(string_ref().empty());
}

ZEST_CASE(formats_like_a_view) {
    EXPECT(std::format("[{:>5}]", string_ref("abc")) == "[  abc]");
}

ZEST_CASE(compares_ignoring_ascii_case) {
    EXPECT(string_ref("Hello").compare_insensitive("hELLO") == 0);
    EXPECT(string_ref("abc").compare_insensitive("ABD") < 0);
    EXPECT(string_ref("abd").compare_insensitive("ABC") > 0);
    EXPECT(string_ref("ab").compare_insensitive("ABC") < 0);
    EXPECT(string_ref("abc").compare_insensitive("AB") > 0);
    EXPECT(string_ref("\xC3\xA9").compare_insensitive("\xC3\x89") != 0);
}

ZEST_CASE(equals_ignoring_ascii_case) {
    EXPECT(string_ref("Kotatsu").equals_insensitive("kOTATSU"));
    EXPECT(!string_ref("Kotatsu").equals_insensitive("Kotatsu!"));
}

ZEST_CASE(prefixes_and_suffixes_ignoring_case) {
    string_ref text("Header.H");
    EXPECT(text.starts_with_insensitive("HEAD"));
    EXPECT(!text.starts_with_insensitive("header.h.x"));
    EXPECT(text.ends_with_insensitive(".h"));
    EXPECT(!text.ends_with_insensitive(".c"));
    EXPECT(!string_ref("h").ends_with_insensitive(".h"));
}

ZEST_CASE(contains_finds_text_and_characters) {
    string_ref text("needle in haystack");
    EXPECT(text.contains("in hay"));
    EXPECT(text.contains('k'));
    EXPECT(!text.contains("pin"));
    EXPECT(!text.contains('z'));
    EXPECT(text.contains_insensitive("HAYSTACK"));
    EXPECT(!text.contains_insensitive("HAYSTACKS"));
}

ZEST_CASE(find_by_predicate) {
    string_ref text("ab1c2");
    auto digit = [](char c) {
        return c >= '0' && c <= '9';
    };
    EXPECT(text.find_if(digit) == 2U);
    EXPECT(text.find_if(digit, 3) == 4U);
    EXPECT(text.find_if(digit, 100) == string_ref::npos);
    EXPECT(text.find_if_not(digit, 2) == 3U);
    EXPECT(string_ref("123").find_if_not(digit) == string_ref::npos);
}

ZEST_CASE(find_ignoring_case) {
    string_ref text("aXbxC");
    EXPECT(text.find_insensitive('x') == 1U);
    EXPECT(text.find_insensitive('X', 2) == 3U);
    EXPECT(text.find_insensitive('z') == string_ref::npos);
    EXPECT(text.find_insensitive("BXc") == 2U);
    EXPECT(text.find_insensitive("xb", 2) == string_ref::npos);
    EXPECT(text.find_insensitive("") == 0U);
}

ZEST_CASE(rfind_ignoring_case) {
    string_ref text("aXbxC");
    EXPECT(text.rfind_insensitive('X') == 3U);
    EXPECT(text.rfind_insensitive('x', 2) == 1U);
    EXPECT(text.rfind_insensitive('z') == string_ref::npos);
    EXPECT(string_ref().rfind_insensitive('a') == string_ref::npos);
    EXPECT(text.rfind_insensitive("XB") == 1U);
    EXPECT(text.rfind_insensitive("xc") == 3U);
    EXPECT(text.rfind_insensitive("longer than the text") == string_ref::npos);
}

ZEST_CASE(count_characters_and_strings) {
    string_ref text("abababa");
    EXPECT(text.count('a') == 4U);
    EXPECT(text.count('z') == 0U);
    // Non-overlapping occurrences.
    EXPECT(text.count("aba") == 2U);
    EXPECT(text.count("") == 0U);
}

ZEST_CASE(substr_and_slice_clamp_to_the_text) {
    string_ref text("hello");
    EXPECT(text.substr(1, 3) == "ell");
    EXPECT(text.substr(3) == "lo");
    EXPECT(text.substr(10).empty());
    EXPECT(text.slice(1, 4) == "ell");
    EXPECT(text.slice(3, 1).empty());
    EXPECT(text.slice(2, 100) == "llo");
}

ZEST_CASE(take_and_drop_clamp_to_the_text) {
    string_ref text("hello");
    EXPECT(text.take_front(2) == "he");
    EXPECT(text.take_front(10) == "hello");
    EXPECT(text.take_back(2) == "lo");
    EXPECT(text.take_back(10) == "hello");
    EXPECT(text.drop_front(2) == "llo");
    EXPECT(text.drop_back(2) == "hel");
    EXPECT(text.drop_front() == "ello");
    EXPECT(text.drop_back() == "hell");
}

ZEST_CASE(take_and_drop_by_predicate) {
    string_ref text("  indented");
    auto space = [](char c) {
        return c == ' ';
    };
    EXPECT(text.take_while(space) == "  ");
    EXPECT(text.drop_while(space) == "indented");
    EXPECT(text.take_until([](char c) { return c == 'd'; }) == "  in");
    EXPECT(text.drop_until([](char c) { return c == 'd'; }) == "dented");
}

ZEST_CASE(consume_affixes) {
    string_ref text("--flag=value");
    EXPECT(text.consume_front("--"));
    EXPECT(text == "flag=value");
    EXPECT(!text.consume_front("--"));
    EXPECT(text.consume_back("=value"));
    EXPECT(text == "flag");
    EXPECT(!text.consume_back("x"));
    EXPECT(text == "flag");
}

ZEST_CASE(consume_affixes_ignoring_case) {
    string_ref text("HTTP://Host.COM");
    EXPECT(text.consume_front_insensitive("http://"));
    EXPECT(text.consume_back_insensitive(".com"));
    EXPECT(text == "Host");
    EXPECT(!text.consume_front_insensitive("x"));
    EXPECT(!text.consume_back_insensitive("x"));
}

ZEST_CASE(trim_removes_a_character_from_either_end) {
    string_ref text("xxmiddlexx");
    EXPECT(text.ltrim('x') == "middlexx");
    EXPECT(text.rtrim('x') == "xxmiddle");
    EXPECT(text.trim('x') == "middle");
    EXPECT(string_ref("xxx").trim('x').empty());
}

ZEST_CASE(trim_character_sets) {
    string_ref text(" \t middle \r\n");
    EXPECT(text.ltrim() == "middle \r\n");
    EXPECT(text.rtrim() == " \t middle");
    EXPECT(text.trim() == "middle");
    EXPECT(string_ref("-+x+-").trim("+-") == "x");
    EXPECT(string_ref(" \n").trim().empty());
}

ZEST_CASE(split_at_the_first_separator) {
    string_ref text("key=value=more");
    EXPECT(strings(text.split('=')) == std::pair<std::string, std::string>{"key", "value=more"});
    EXPECT(strings(text.split("=v")) == std::pair<std::string, std::string>{"key", "alue=more"});
    EXPECT(strings(text.split(':')) == std::pair<std::string, std::string>{"key=value=more", ""});
    EXPECT(strings(text.split("::")) == std::pair<std::string, std::string>{"key=value=more", ""});
}

ZEST_CASE(split_at_the_last_separator) {
    string_ref text("a/b/c");
    EXPECT(strings(text.rsplit('/')) == std::pair<std::string, std::string>{"a/b", "c"});
    EXPECT(strings(text.rsplit("/b")) == std::pair<std::string, std::string>{"a", "/c"});
    EXPECT(strings(text.rsplit('.')) == std::pair<std::string, std::string>{"a/b/c", ""});
    EXPECT(strings(text.rsplit("..")) == std::pair<std::string, std::string>{"a/b/c", ""});
}

ZEST_CASE(split_into_parts_by_character) {
    EXPECT(split_all("a,b,,c", ',') == std::vector<std::string>{"a", "b", "", "c"});
    EXPECT(split_all("a,b,,c", ',', -1, false) == std::vector<std::string>{"a", "b", "c"});
    EXPECT(split_all("a,b,c", ',', 1) == std::vector<std::string>{"a", "b,c"});
    EXPECT(split_all("a,b,c", ',', 0) == std::vector<std::string>{"a,b,c"});
    EXPECT(split_all(",", ',', -1, false).empty());
}

ZEST_CASE(split_into_parts_by_string) {
    EXPECT(split_all("a::b::::c", "::") == std::vector<std::string>{"a", "b", "", "c"});
    EXPECT(split_all("a::b::::c", "::", -1, false) == std::vector<std::string>{"a", "b", "c"});
    EXPECT(split_all("a::b::c", "::", 1) == std::vector<std::string>{"a", "b::c"});
    // An empty separator hands over the whole text.
    EXPECT(split_all("abc", "") == std::vector<std::string>{"abc"});
}

ZEST_CASE(change_ascii_case) {
    string_ref text("MiXeD 123 \xC3\xA9");
    EXPECT(text.lower() == "mixed 123 \xC3\xA9");
    EXPECT(text.upper() == "MIXED 123 \xC3\xA9");
    EXPECT(text.str() == "MiXeD 123 \xC3\xA9");
}

ZEST_CASE(keeps_the_overloads_of_string_view) {
    string_ref text("path/to/  ");
    EXPECT(text.find_last_not_of(" /") == 6U);
    EXPECT(text.find_last_not_of(' ') == 7U);
    EXPECT(text.compare(0, 4, "path") == 0);
    EXPECT(text.compare("path") > 0);
}

};  // ZEST_SUITE(support_string_ref)

}  // namespace

}  // namespace kota
