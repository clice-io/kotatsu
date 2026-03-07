#pragma once

#include <optional>

#include "spelling.h"
#include "eventide/common/fixed_string.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/serde/attrs/behavior.h"
#include "eventide/serde/serde/attrs/hint.h"
#include "eventide/serde/serde/attrs/schema.h"

namespace eventide::serde {

template <typename T>
using skip = annotation<T, schema::skip>;

template <typename T>
using flatten = annotation<T, schema::flatten>;

template <typename T, fixed_string Name>
using literal = annotation<T, schema::literal<Name>>;

template <typename T, fixed_string Name>
using rename = annotation<T, schema::rename<Name>>;

template <typename T, fixed_string Name, fixed_string... AliasNames>
using rename_alias = annotation<T, schema::rename<Name>, schema::alias<AliasNames...>>;

template <typename T, fixed_string... Names>
using alias = annotation<T, schema::alias<Names...>>;

template <typename T, typename Pred>
using skip_if = annotation<T, behavior::skip_if<Pred>>;

template <typename E, typename Policy = rename_policy::lower_camel>
using enum_string = annotation<E, behavior::enum_string<Policy>>;

template <typename T>
using skip_if_none = annotation<std::optional<T>, behavior::skip_if<pred::optional_none>>;

template <typename T>
using skip_if_empty = annotation<T, behavior::skip_if<pred::empty>>;

template <typename T>
using skip_if_default = annotation<T, behavior::skip_if<pred::default_value>>;

}  // namespace eventide::serde
