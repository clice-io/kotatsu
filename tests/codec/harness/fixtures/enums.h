#pragma once

// Enum fixtures only the codec's tests use.

#include <cstdint>

#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::test {

enum class SignedEnum : std::int32_t { neg = -42, zero = 0, pos = 42 };

enum class UInt8Enum : std::uint8_t { a = 0, b = 1, c = 255 };

/// A char underlying type: the value is still an integer.
enum class Letter : char { a = 'A', z = 'Z' };

/// Enumerator names of several words, so that rename policies show.
enum class Access : std::uint8_t { read_only, full_control };

/// Access, travelling as its enumerators' names (an enum_names repr).
enum class NamedAccess : std::uint8_t { read_only, full_control };

enum class CamelAccess : std::uint8_t;
KANNOTATE_TYPE(CamelAccess, rename_all = Casing::LowerCamel);

/// NamedAccess, its names spelled by the rename_all of its own annotation.
enum class CamelAccess : std::uint8_t { read_only, full_control };

/// Enumerators declared in several cases, in lower camel case `fullControl`, `readOnly`,
/// `httpServer` and `noAccess`.
enum class Mixed : std::uint8_t { FullControl, readOnly, HTTPServer, no_access };

}  // namespace kotatsu::test

template <>
struct kotatsu::codec::repr<kotatsu::test::NamedAccess> : codec::enum_names<> {};

template <>
struct kotatsu::codec::repr<kotatsu::test::CamelAccess> : codec::enum_names<> {};
