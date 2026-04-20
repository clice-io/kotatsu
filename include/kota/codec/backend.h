#pragma once

namespace kota::codec {

enum class backend_kind { streaming, arena };

enum class field_mode { by_name, by_position, by_tag };

template <typename T>
concept has_backend_kind = requires { T::backend_kind_v; };

template <typename T>
concept has_field_mode = requires { T::field_mode_v; };

}  // namespace kota::codec
