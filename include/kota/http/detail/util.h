#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "kota/http/detail/common.h"

namespace kota::http::detail {

/// Whether `lhs` and `rhs` are equal, ignoring the case of ASCII letters.
bool iequals(std::string_view lhs, std::string_view rhs) noexcept;

/// Sets header `name` to `value`: replaces the first header of that name in
/// any case, name spelling included, or appends one.
void upsert_header(std::vector<header>& headers, std::string name, std::string value);

/// Appends header `name` unless one of that name in any case is there.
void insert_header(std::vector<header>& headers, std::string name, std::string value);

/// `text` without the ASCII whitespace at either end.
std::string trim_ascii(std::string_view text);

/// `text` with every byte but the unreserved characters of RFC 3986
/// (letters, digits, `-`, `.`, `_`, `~`) written as `%XX`.
std::string percent_encode(std::string_view text);

/// `pairs` as `name=value&...`, each part percent-encoded.
std::string encode_pairs(const std::vector<query_param>& pairs);

/// `text` in base64 (RFC 4648), padded.
std::string base64_encode(std::string_view text);

}  // namespace kota::http::detail
