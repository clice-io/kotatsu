#pragma once

// Schema-attr fixtures — rename / skip / alias / flatten / literal /
// default_value / deny_unknown_fields.

#include <string>

#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::meta::fixtures {

struct AnnotatedStruct {
    annotation<int, attrs::rename<"id">> user_id;
    annotation<std::string, attrs::skip> internal;
    float value;
};

struct AliasStruct {
    annotation<int, attrs::alias<"user_id", "userId">> id;
    std::string name;
};

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    annotation<Inner, attrs::flatten> inner;
    int y;
};

struct FlattenTailStruct {
    int head;
    int neck;
    annotation<Inner, attrs::flatten> body;
};

struct DeepInner {
    int p;
    int q;
};

struct Middle {
    int m;
    annotation<DeepInner, attrs::flatten> deep;
};

struct DeepOuter {
    int head;
    annotation<Middle, attrs::flatten> mid;
    int tail;
};

struct FlattenInnerWithSkip {
    int keep_a;
    annotation<int, attrs::skip> drop_b;
    int keep_c;
};

struct FlattenOuterWithChildSkip {
    int head;
    annotation<FlattenInnerWithSkip, attrs::flatten> inner;
};

struct FlattenInnerWithRename {
    annotation<int, attrs::rename<"renamed_a">> a;
    int b;
};

struct FlattenOuterWithChildRename {
    annotation<FlattenInnerWithRename, attrs::flatten> inner;
};

struct DefaultLiteralStruct {
    annotation<int, attrs::default_value> with_default;
    annotation<std::string, attrs::literal<"v1">> version;
    int plain;
};

struct RenameTarget {
    int user_name;
    std::string display_name;
};

using RenamedRoot = annotation<RenameTarget, attrs::rename_all<rename_policy::lower_camel>>;
using StrictRoot = annotation<RenameTarget, attrs::deny_unknown_fields>;

}  // namespace kota::meta::fixtures
