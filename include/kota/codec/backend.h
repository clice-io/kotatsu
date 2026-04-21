#pragma once

namespace kota::codec {

enum class backend_kind { streaming, arena };

enum class field_mode { by_name, by_position, by_tag };

}  // namespace kota::codec
