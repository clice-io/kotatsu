#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace kota {

/// A string_view extension with convenient string manipulation methods.
/// Inherits from std::string_view and adds operations like split, trim,
/// take/drop, consume, count, find_if, etc. All methods are constexpr.
class string_ref : public std::string_view {
    using base = std::string_view;

    constexpr static int compare_memory(const char* lhs, const char* rhs, std::size_t length) {
        if(length == 0) {
            return 0;
        }
        for(std::size_t i = 0; i < length; ++i) {
            auto l = static_cast<unsigned char>(lhs[i]);
            auto r = static_cast<unsigned char>(rhs[i]);
            if(l != r) {
                return l < r ? -1 : 1;
            }
        }
        return 0;
    }

    constexpr static char to_lower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    constexpr static char to_upper(char c) {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }

    constexpr static int ascii_strncasecmp(string_ref lhs, string_ref rhs, std::size_t length) {
        for(std::size_t i = 0; i < length; ++i) {
            auto l = static_cast<unsigned char>(to_lower(lhs[i]));
            auto r = static_cast<unsigned char>(to_lower(rhs[i]));
            if(l != r) {
                return l < r ? -1 : 1;
            }
        }
        return 0;
    }

public:
    using base::npos;
    using base::base;

    constexpr string_ref() = default;

    constexpr string_ref(std::string_view sv) : base(sv) {}

    constexpr string_ref(const std::string& s) : base(s) {}

    // --- Comparison ---

    /// Compare two strings; result is negative, zero, or positive.
    [[nodiscard]] constexpr int compare(string_ref rhs) const {
        if(int res = compare_memory(data(), rhs.data(), std::min(size(), rhs.size()))) {
            return res;
        }
        if(size() == rhs.size()) {
            return 0;
        }
        return size() < rhs.size() ? -1 : 1;
    }

    /// Compare two strings, ignoring case.
    [[nodiscard]] constexpr int compare_insensitive(string_ref rhs) const {
        auto min = std::min(size(), rhs.size());
        if(int res = ascii_strncasecmp(*this, rhs, min)) {
            return res;
        }
        if(size() == rhs.size()) {
            return 0;
        }
        return size() < rhs.size() ? -1 : 1;
    }

    /// Check for string equality, ignoring case.
    [[nodiscard]] constexpr bool equals_insensitive(string_ref rhs) const {
        return size() == rhs.size() && compare_insensitive(rhs) == 0;
    }

    // --- Predicates ---

    /// Check if this string starts with the given prefix, ignoring case.
    [[nodiscard]] constexpr bool starts_with_insensitive(string_ref prefix) const {
        return size() >= prefix.size() && ascii_strncasecmp(*this, prefix, prefix.size()) == 0;
    }

    /// Check if this string ends with the given suffix, ignoring case.
    [[nodiscard]] constexpr bool ends_with_insensitive(string_ref suffix) const {
        return size() >= suffix.size() &&
               ascii_strncasecmp(drop_front(size() - suffix.size()), suffix, suffix.size()) == 0;
    }

    /// Return true if the given string is a substring of *this.
    [[nodiscard]] constexpr bool contains(string_ref other) const {
        return find(other) != npos;
    }

    /// Return true if the given character is contained in *this.
    [[nodiscard]] constexpr bool contains(char c) const {
        return find(c) != npos;
    }

    /// Return true if the given string is a substring of *this, ignoring case.
    [[nodiscard]] constexpr bool contains_insensitive(string_ref other) const {
        return find_insensitive(other) != npos;
    }

    // --- Searching ---

    /// Search for the first character satisfying the predicate.
    template <typename F>
    [[nodiscard]] constexpr std::size_t find_if(F f, std::size_t from = 0) const {
        from = std::min(from, size());
        string_ref s = drop_front(from);
        while(!s.empty()) {
            if(f(s.front())) {
                return size() - s.size();
            }
            s = s.drop_front();
        }
        return npos;
    }

    /// Search for the first character not satisfying the predicate.
    template <typename F>
    [[nodiscard]] constexpr std::size_t find_if_not(F f, std::size_t from = 0) const {
        return find_if([&](char c) { return !f(c); }, from);
    }

    /// Search for the first character, ignoring case.
    [[nodiscard]] constexpr std::size_t find_insensitive(char c, std::size_t from = 0) const {
        char l = to_lower(c);
        return find_if([l](char d) { return to_lower(d) == l; }, from);
    }

    /// Search for the first occurrence of a string, ignoring case.
    [[nodiscard]] constexpr std::size_t find_insensitive(string_ref str,
                                                         std::size_t from = 0) const {
        from = std::min(from, size());
        string_ref s = drop_front(from);
        while(s.size() >= str.size()) {
            if(s.starts_with_insensitive(str)) {
                return from;
            }
            s = s.drop_front();
            ++from;
        }
        return npos;
    }

    /// Search for the last character, ignoring case.
    [[nodiscard]] constexpr std::size_t rfind_insensitive(char c, std::size_t from = npos) const {
        if(empty()) {
            return npos;
        }
        from = std::min(from, size() - 1);
        for(std::size_t i = from + 1; i != 0;) {
            --i;
            if(to_lower(data()[i]) == to_lower(c)) {
                return i;
            }
        }
        return npos;
    }

    /// Search for the last occurrence of a string, ignoring case.
    [[nodiscard]] constexpr std::size_t rfind_insensitive(string_ref str) const {
        std::size_t n = str.size();
        if(n > size()) {
            return npos;
        }
        for(std::size_t i = size() - n + 1; i != 0;) {
            --i;
            if(substr(i, n).equals_insensitive(str)) {
                return i;
            }
        }
        return npos;
    }

    /// Find the last character in the string that is not c, or npos if not found.
    [[nodiscard]] constexpr std::size_t find_last_not_of(char c, std::size_t from = npos) const {
        if(empty()) {
            return npos;
        }
        from = std::min(from, size() - 1);
        for(std::size_t i = from + 1; i != 0;) {
            --i;
            if(data()[i] != c) {
                return i;
            }
        }
        return npos;
    }

    // --- Count ---

    /// Return the number of occurrences of c in the string.
    [[nodiscard]] constexpr std::size_t count(char c) const {
        std::size_t result = 0;
        for(std::size_t i = 0; i < size(); ++i) {
            if(data()[i] == c) {
                ++result;
            }
        }
        return result;
    }

    /// Return the number of non-overlapped occurrences of str in the string.
    [[nodiscard]] constexpr std::size_t count(string_ref str) const {
        std::size_t result = 0;
        std::size_t n = str.size();
        if(n == 0) {
            return 0;
        }
        std::size_t pos = 0;
        while((pos = find(str, pos)) != npos) {
            ++result;
            pos += n;
        }
        return result;
    }

    // --- Substring / Slice ---

    /// Return a reference to the substring from [start, start + n).
    [[nodiscard]] constexpr string_ref substr(std::size_t start, std::size_t n = npos) const {
        start = std::min(start, size());
        return string_ref(data() + start, std::min(n, size() - start));
    }

    /// Return a reference to the substring from [start, end).
    [[nodiscard]] constexpr string_ref slice(std::size_t start, std::size_t end) const {
        start = std::min(start, size());
        end = std::clamp(end, start, size());
        return string_ref(data() + start, end - start);
    }

    // --- Take / Drop ---

    /// Return a string_ref with only the first n elements remaining.
    [[nodiscard]] constexpr string_ref take_front(std::size_t n = 1) const {
        if(n >= size()) {
            return *this;
        }
        return drop_back(size() - n);
    }

    /// Return a string_ref with only the last n elements remaining.
    [[nodiscard]] constexpr string_ref take_back(std::size_t n = 1) const {
        if(n >= size()) {
            return *this;
        }
        return drop_front(size() - n);
    }

    /// Return the longest prefix where every character satisfies the predicate.
    template <typename F>
    [[nodiscard]] constexpr string_ref take_while(F f) const {
        return substr(0, find_if_not(f));
    }

    /// Return the longest prefix where no character satisfies the predicate.
    template <typename F>
    [[nodiscard]] constexpr string_ref take_until(F f) const {
        return substr(0, find_if(f));
    }

    /// Return a string_ref with the first n elements dropped.
    [[nodiscard]] constexpr string_ref drop_front(std::size_t n = 1) const {
        assert(size() >= n && "Dropping more elements than exist");
        return substr(n);
    }

    /// Return a string_ref with the last n elements dropped.
    [[nodiscard]] constexpr string_ref drop_back(std::size_t n = 1) const {
        assert(size() >= n && "Dropping more elements than exist");
        return substr(0, size() - n);
    }

    /// Drop characters from the front while they satisfy the predicate.
    template <typename F>
    [[nodiscard]] constexpr string_ref drop_while(F f) const {
        return substr(find_if_not(f));
    }

    /// Drop characters from the front until one satisfies the predicate.
    template <typename F>
    [[nodiscard]] constexpr string_ref drop_until(F f) const {
        return substr(find_if(f));
    }

    // --- Consume ---

    /// If this string starts with prefix, remove it and return true.
    constexpr bool consume_front(string_ref prefix) {
        if(!starts_with(prefix)) {
            return false;
        }
        *this = drop_front(prefix.size());
        return true;
    }

    /// If this string starts with prefix (ignoring case), remove it and return true.
    constexpr bool consume_front_insensitive(string_ref prefix) {
        if(!starts_with_insensitive(prefix)) {
            return false;
        }
        *this = drop_front(prefix.size());
        return true;
    }

    /// If this string ends with suffix, remove it and return true.
    constexpr bool consume_back(string_ref suffix) {
        if(!ends_with(suffix)) {
            return false;
        }
        *this = drop_back(suffix.size());
        return true;
    }

    /// If this string ends with suffix (ignoring case), remove it and return true.
    constexpr bool consume_back_insensitive(string_ref suffix) {
        if(!ends_with_insensitive(suffix)) {
            return false;
        }
        *this = drop_back(suffix.size());
        return true;
    }

    // --- Trim ---

    /// Return string with consecutive char characters starting from the left removed.
    [[nodiscard]] constexpr string_ref ltrim(char c) const {
        return drop_front(std::min(size(), base::find_first_not_of(c)));
    }

    /// Return string with consecutive characters in chars starting from the left removed.
    [[nodiscard]] constexpr string_ref ltrim(string_ref chars = " \t\n\v\f\r") const {
        return drop_front(std::min(size(), base::find_first_not_of(chars)));
    }

    /// Return string with consecutive char characters starting from the right removed.
    [[nodiscard]] constexpr string_ref rtrim(char c) const {
        return drop_back(size() - std::min(size(), find_last_not_of(c) + 1));
    }

    /// Return string with consecutive characters in chars starting from the right removed.
    [[nodiscard]] constexpr string_ref rtrim(string_ref chars = " \t\n\v\f\r") const {
        return drop_back(size() - std::min(size(), base::find_last_not_of(chars) + 1));
    }

    /// Return string with consecutive char characters from both sides removed.
    [[nodiscard]] constexpr string_ref trim(char c) const {
        return ltrim(c).rtrim(c);
    }

    /// Return string with consecutive characters in chars from both sides removed.
    [[nodiscard]] constexpr string_ref trim(string_ref chars = " \t\n\v\f\r") const {
        return ltrim(chars).rtrim(chars);
    }

    // --- Split ---

    /// Split into two substrings around the first occurrence of a separator character.
    [[nodiscard]] constexpr std::pair<string_ref, string_ref> split(char separator) const {
        std::size_t idx = find(separator);
        if(idx == npos) {
            return {*this, string_ref()};
        }
        return {slice(0, idx), substr(idx + 1)};
    }

    /// Split into two substrings around the first occurrence of a separator string.
    [[nodiscard]] constexpr std::pair<string_ref, string_ref> split(string_ref separator) const {
        std::size_t idx = find(separator);
        if(idx == npos) {
            return {*this, string_ref()};
        }
        return {slice(0, idx), substr(idx + separator.size())};
    }

    /// Split into two substrings around the last occurrence of a separator character.
    [[nodiscard]] constexpr std::pair<string_ref, string_ref> rsplit(char separator) const {
        std::size_t idx = rfind(separator);
        if(idx == npos) {
            return {*this, string_ref()};
        }
        return {slice(0, idx), substr(idx + 1)};
    }

    /// Split into two substrings around the last occurrence of a separator string.
    [[nodiscard]] constexpr std::pair<string_ref, string_ref> rsplit(string_ref separator) const {
        std::size_t idx = rfind(separator);
        if(idx == npos) {
            return {*this, string_ref()};
        }
        return {slice(0, idx), substr(idx + separator.size())};
    }

    /// Split into substrings around occurrences of a separator.
    /// Calls callback(string_ref) for each part.
    template <typename Callback>
    constexpr void
        split(Callback callback, char separator, int max_split = -1, bool keep_empty = true) const {
        string_ref s = *this;
        while(max_split-- != 0) {
            std::size_t idx = s.find(separator);
            if(idx == npos) {
                break;
            }
            if(keep_empty || idx > 0) {
                callback(s.slice(0, idx));
            }
            s = s.substr(idx + 1);
        }
        if(keep_empty || !s.empty()) {
            callback(s);
        }
    }

    /// Split into substrings around occurrences of a separator string.
    /// Calls callback(string_ref) for each part.
    /// If separator is empty, calls callback once with the entire string.
    template <typename Callback>
    constexpr void split(Callback callback,
                         string_ref separator,
                         int max_split = -1,
                         bool keep_empty = true) const {
        if(separator.empty()) {
            callback(*this);
            return;
        }
        string_ref s = *this;
        while(max_split-- != 0) {
            std::size_t idx = s.find(separator);
            if(idx == npos) {
                break;
            }
            if(keep_empty || idx > 0) {
                callback(s.slice(0, idx));
            }
            s = s.substr(idx + separator.size());
        }
        if(keep_empty || !s.empty()) {
            callback(s);
        }
    }

    // --- Case Conversion ---

    /// Convert the string to lowercase (returns std::string).
    [[nodiscard]] constexpr std::string lower() const {
        std::string result;
        result.reserve(size());
        for(char c: *this) {
            result.push_back(to_lower(c));
        }
        return result;
    }

    /// Convert the string to uppercase (returns std::string).
    [[nodiscard]] constexpr std::string upper() const {
        std::string result;
        result.reserve(size());
        for(char c: *this) {
            result.push_back(to_upper(c));
        }
        return result;
    }

    // --- Conversion ---

    /// Get the contents as an std::string.
    [[nodiscard]] constexpr std::string str() const {
        return std::string(data(), size());
    }
};

}  // namespace kota
