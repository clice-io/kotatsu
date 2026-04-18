#pragma once

// Backend-tag hint fixtures for `hint<BackendTag, ...>` and `get_hint_t`.

#include <string>

#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::meta::fixtures {

struct JsonBackend {};

struct TomlBackend {};

struct FlatbuffersBackend {};

struct HintInline {};

struct HintInlineSize {};

struct HintRequired {};

struct SingleHintStruct {
    annotation<int, hint<JsonBackend, HintRequired>> id;
};

struct MultiBackendHintStruct {
    annotation<int, hint<JsonBackend, HintRequired>, hint<TomlBackend, HintInline>> id;
    annotation<std::string, hint<FlatbuffersBackend, HintInline, HintInlineSize>> name;
};

}  // namespace kota::meta::fixtures
