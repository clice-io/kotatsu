#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "kota/http/detail/common.h"

namespace kota::http::detail {

bool iequals(std::string_view lhs, std::string_view rhs) noexcept;

void upsert_header(std::vector<header>& headers, std::string name, std::string value);

void insert_header(std::vector<header>& headers, std::string name, std::string value);

std::string trim_ascii(std::string_view text);

std::string lower_ascii(std::string_view text);

std::string percent_encode(std::string_view text);

std::string encode_pairs(const std::vector<query_param>& pairs);

std::string base64_encode(std::string_view text);

}  // namespace kota::http::detail
