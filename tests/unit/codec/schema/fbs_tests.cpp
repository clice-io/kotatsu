#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/schema/fbs.h"

namespace kota::meta {

namespace {

// ---------------------------------------------------------------------------
// Scalar wrappers
// ---------------------------------------------------------------------------
struct s_bool {
    bool v;
};

struct s_i8 {
    std::int8_t v;
};

struct s_i16 {
    std::int16_t v;
};

struct s_i32 {
    std::int32_t v;
};

struct s_i64 {
    std::int64_t v;
};

struct s_u8 {
    std::uint8_t v;
};

struct s_u16 {
    std::uint16_t v;
};

struct s_u32 {
    std::uint32_t v;
};

struct s_u64 {
    std::uint64_t v;
};

struct s_f32 {
    float v;
};

struct s_f64 {
    double v;
};

struct s_char {
    char v;
};

struct s_str {
    std::string v;
};

struct s_bytes {
    std::vector<std::byte> v;
};

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class color_i8 : std::int8_t { red = 0, green = 1, blue = 2 };
enum class single_enum : std::int32_t { only = 42 };
enum class status : std::int32_t { ok = 0, fail = 1, pending = 2 };
enum class flag_u8 : std::uint8_t { off = 0, on = 1 };
enum class level_i16 : std::int16_t { low = 0, mid = 50, high = 100 };
enum class huge_u64 : std::uint64_t {
    zero = 0,
    max = std::numeric_limits<std::uint64_t>::max(),
};

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------
struct s_vec_i32 {
    std::vector<std::int32_t> v;
};

struct s_set_i32 {
    std::set<std::int32_t> v;
};

struct s_map_str_i32 {
    std::map<std::string, std::int32_t> v;
};

struct s_vec_vec_i32 {
    std::vector<std::vector<std::int32_t>> v;
};

struct s_map_str_vec_i32 {
    std::map<std::string, std::vector<std::int32_t>> v;
};

// ---------------------------------------------------------------------------
// Tuple / Pair
// ---------------------------------------------------------------------------
struct s_pair {
    std::pair<std::string, std::int32_t> v;
};

struct s_tuple {
    std::tuple<std::int32_t, std::string, bool> v;
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------
struct empty_struct {};

struct single_field {
    std::int32_t x;
};

struct point2d {
    std::int32_t x;
    std::int32_t y;
};

struct with_string {
    std::string name;
    std::int32_t value;
};

// ---------------------------------------------------------------------------
// Nested structs
// ---------------------------------------------------------------------------
struct inner {
    std::int32_t a;
};

struct middle {
    inner i;
    std::string s;
};

struct outer {
    middle m;
    std::int32_t n;
};

// ---------------------------------------------------------------------------
// Struct with enum
// ---------------------------------------------------------------------------
struct with_enum {
    color_i8 c;
    std::string name;
};

// ---------------------------------------------------------------------------
// Optional / pointer
// ---------------------------------------------------------------------------
struct with_optional {
    std::string name;
    std::optional<std::int32_t> age;
};

struct with_unique {
    std::string name;
    std::unique_ptr<std::int32_t> ptr;
};

struct with_shared {
    std::string name;
    std::shared_ptr<std::int32_t> ptr;
};

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
struct with_default {
    std::string name;
    annotation<std::int32_t, attrs::default_value> count;
};

struct with_skip {
    std::string visible;
    annotation<std::int32_t, attrs::skip> hidden;
};

struct base_fields {
    std::int32_t a;
    std::int32_t b;
};

struct with_flatten {
    annotation<base_fields, attrs::flatten> base;
    std::string extra;
};

struct with_rename {
    annotation<std::int32_t, attrs::rename<"my_field">> x;
    std::string y;
};

// ---------------------------------------------------------------------------
// Variant
// ---------------------------------------------------------------------------
struct var_none {
    std::variant<std::int32_t, std::string> v;
};

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------
struct combo {
    color_i8 color;
    std::optional<std::string> label;
    std::vector<std::int32_t> values;
    std::map<std::string, std::int32_t> attrs;
};

struct nested_combo {
    point2d point;
    color_i8 color;
    std::vector<point2d> points;
    std::map<std::string, point2d> named_points;
};

struct multi_map {
    std::map<std::string, std::int32_t> a;
    std::map<std::string, std::string> b;
};

struct vec_of_struct {
    std::vector<point2d> items;
};

struct deep_inner {
    color_i8 c;
    std::int32_t v;
};

struct deep_middle {
    deep_inner di;
    std::string s;
};

struct deep_outer {
    deep_middle dm;
    std::int32_t n;
};

// ---------------------------------------------------------------------------
// Additional types
// ---------------------------------------------------------------------------
struct all_optional {
    std::optional<std::int32_t> a;
    std::optional<std::string> b;
};

struct all_default {
    annotation<std::int32_t, attrs::default_value> x;
    annotation<std::string, attrs::default_value> y;
};

struct skip_default {
    std::string name;
    annotation<std::int32_t, attrs::skip> hidden;
    annotation<std::int32_t, attrs::default_value> count;
};

struct base_with_opt {
    std::int32_t x;
    std::optional<std::int32_t> y;
};

struct flatten_opt {
    annotation<base_with_opt, attrs::flatten> base;
    std::string tag;
};

struct rename_base {
    annotation<std::int32_t, attrs::rename<"alpha">> a;
    std::int32_t b;
};

struct flatten_rename {
    annotation<rename_base, attrs::flatten> inner;
    std::string extra;
};

struct map_str_struct {
    std::map<std::string, point2d> entries;
};

struct map_str_enum {
    std::map<std::string, color_i8> entries;
};

struct vec_optional {
    std::vector<std::optional<std::int32_t>> v;
};

struct optional_vec {
    std::optional<std::vector<std::int32_t>> v;
};

struct with_pair_field {
    std::pair<std::string, std::int32_t> p;
    std::string name;
};

struct with_tuple_field {
    std::tuple<std::int32_t, bool> t;
    std::string name;
};

struct shared_struct {
    std::string name;
    std::shared_ptr<point2d> point;
};

struct multi_ref {
    point2d a;
    point2d b;
    std::vector<point2d> list;
};

struct vec_enum {
    std::vector<color_i8> colors;
};

struct set_string {
    std::set<std::string> tags;
};

struct optional_struct {
    std::optional<point2d> point;
    std::string name;
};

struct vec_map {
    std::vector<std::map<std::string, std::int32_t>> items;
};

struct map_vec_struct {
    std::map<std::string, std::vector<point2d>> groups;
};

struct trivial_nested {
    point2d p;
    std::int32_t z;
};

struct multi_enum {
    color_i8 c;
    status s;
    std::string label;
};

struct with_flag {
    flag_u8 f;
    std::string name;
};

struct with_level {
    level_i16 l;
    std::int32_t v;
};

struct with_huge_u64 {
    huge_u64 e;
};

struct with_all_ptr {
    std::optional<std::string> opt;
    std::unique_ptr<std::int32_t> uniq;
    std::shared_ptr<bool> shr;
};

struct deep_container {
    std::map<std::string, std::vector<std::map<std::string, std::int32_t>>> data;
};

struct optional_inner {
    std::optional<inner> i;
    std::string name;
};

struct map_of_map {
    std::map<std::string, std::map<std::string, std::int32_t>> m;
};

struct vec_variant {
    std::vector<std::variant<std::int32_t, std::string>> items;
};

struct many_fields {
    std::int32_t a;
    std::int32_t b;
    std::int32_t c;
    std::string d;
    bool e;
    double f;
};

struct set_of_struct {
    std::set<std::int32_t> ids;
    std::string name;
};

namespace fbs = kota::codec::schema::fbs;

// ===========================================================================
TEST_SUITE(serde_fbs_schema) {

// ---------------------------------------------------------------------------
// Group 1: Scalar structs
// ---------------------------------------------------------------------------

TEST_CASE(scalar_bool) {
    EXPECT_EQ(fbs::render(type_info_of<s_bool>()),
              R"fbs(struct s_bool {
  v:bool;
}

root_type s_bool;
)fbs");
}

TEST_CASE(scalar_i8) {
    EXPECT_EQ(fbs::render(type_info_of<s_i8>()),
              R"fbs(struct s_i8 {
  v:byte;
}

root_type s_i8;
)fbs");
}

TEST_CASE(scalar_i16) {
    EXPECT_EQ(fbs::render(type_info_of<s_i16>()),
              R"fbs(struct s_i16 {
  v:short;
}

root_type s_i16;
)fbs");
}

TEST_CASE(scalar_i32) {
    EXPECT_EQ(fbs::render(type_info_of<s_i32>()),
              R"fbs(struct s_i32 {
  v:int;
}

root_type s_i32;
)fbs");
}

TEST_CASE(scalar_i64) {
    EXPECT_EQ(fbs::render(type_info_of<s_i64>()),
              R"fbs(struct s_i64 {
  v:long;
}

root_type s_i64;
)fbs");
}

TEST_CASE(scalar_u8) {
    EXPECT_EQ(fbs::render(type_info_of<s_u8>()),
              R"fbs(struct s_u8 {
  v:ubyte;
}

root_type s_u8;
)fbs");
}

TEST_CASE(scalar_u16) {
    EXPECT_EQ(fbs::render(type_info_of<s_u16>()),
              R"fbs(struct s_u16 {
  v:ushort;
}

root_type s_u16;
)fbs");
}

TEST_CASE(scalar_u32) {
    EXPECT_EQ(fbs::render(type_info_of<s_u32>()),
              R"fbs(struct s_u32 {
  v:uint;
}

root_type s_u32;
)fbs");
}

TEST_CASE(scalar_u64) {
    EXPECT_EQ(fbs::render(type_info_of<s_u64>()),
              R"fbs(struct s_u64 {
  v:ulong;
}

root_type s_u64;
)fbs");
}

TEST_CASE(scalar_f32) {
    EXPECT_EQ(fbs::render(type_info_of<s_f32>()),
              R"fbs(struct s_f32 {
  v:float;
}

root_type s_f32;
)fbs");
}

TEST_CASE(scalar_f64) {
    EXPECT_EQ(fbs::render(type_info_of<s_f64>()),
              R"fbs(struct s_f64 {
  v:double;
}

root_type s_f64;
)fbs");
}

TEST_CASE(scalar_char) {
    EXPECT_EQ(fbs::render(type_info_of<s_char>()),
              R"fbs(struct s_char {
  v:byte;
}

root_type s_char;
)fbs");
}

TEST_CASE(scalar_str) {
    EXPECT_EQ(fbs::render(type_info_of<s_str>()),
              R"fbs(table s_str {
  v:string;
}

root_type s_str;
)fbs");
}

TEST_CASE(scalar_bytes) {
    EXPECT_EQ(fbs::render(type_info_of<s_bytes>()),
              R"fbs(table s_bytes {
  v:[ubyte];
}

root_type s_bytes;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 2: Basic structs
// ---------------------------------------------------------------------------

TEST_CASE(struct_point2d) {
    EXPECT_EQ(fbs::render(type_info_of<point2d>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

root_type point2d;
)fbs");
}

TEST_CASE(struct_with_string) {
    EXPECT_EQ(fbs::render(type_info_of<with_string>()),
              R"fbs(table with_string {
  name:string;
  value:int;
}

root_type with_string;
)fbs");
}

TEST_CASE(struct_single_field) {
    EXPECT_EQ(fbs::render(type_info_of<single_field>()),
              R"fbs(struct single_field {
  x:int;
}

root_type single_field;
)fbs");
}

TEST_CASE(struct_empty) {
    EXPECT_EQ(fbs::render(type_info_of<empty_struct>()),
              R"fbs(struct empty_struct {
}

root_type empty_struct;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 3: Enum fields
// ---------------------------------------------------------------------------

TEST_CASE(enum_with_enum) {
    EXPECT_EQ(fbs::render(type_info_of<with_enum>()),
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table with_enum {
  c:color_i8;
  name:string;
}

root_type with_enum;
)fbs");
}

TEST_CASE(enum_with_flag) {
    EXPECT_EQ(fbs::render(type_info_of<with_flag>()),
              R"fbs(enum flag_u8:ubyte {
  off = 0,
  on = 1
}

table with_flag {
  f:flag_u8;
  name:string;
}

root_type with_flag;
)fbs");
}

TEST_CASE(enum_with_level) {
    EXPECT_EQ(fbs::render(type_info_of<with_level>()),
              R"fbs(enum level_i16:short {
  low = 0,
  mid = 50,
  high = 100
}

struct with_level {
  l:level_i16;
  v:int;
}

root_type with_level;
)fbs");
}

TEST_CASE(enum_with_huge_u64) {
    EXPECT_EQ(fbs::render(type_info_of<with_huge_u64>()),
              R"fbs(enum huge_u64:ulong {
  max = 18446744073709551615,
  zero = 0
}

struct with_huge_u64 {
  e:huge_u64;
}

root_type with_huge_u64;
)fbs");
}

TEST_CASE(enum_multi_enum) {
    EXPECT_EQ(fbs::render(type_info_of<multi_enum>()),
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

enum status:int {
  ok = 0,
  fail = 1,
  pending = 2
}

table multi_enum {
  c:color_i8;
  s:status;
  label:string;
}

root_type multi_enum;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 4: Attributes
// ---------------------------------------------------------------------------

TEST_CASE(attr_with_default) {
    EXPECT_EQ(fbs::render(type_info_of<with_default>()),
              R"fbs(table with_default {
  name:string;
  count:int;
}

root_type with_default;
)fbs");
}

TEST_CASE(attr_with_skip) {
    EXPECT_EQ(fbs::render(type_info_of<with_skip>()),
              R"fbs(table with_skip {
  visible:string;
}

root_type with_skip;
)fbs");
}

TEST_CASE(attr_with_flatten) {
    EXPECT_EQ(fbs::render(type_info_of<with_flatten>()),
              R"fbs(table with_flatten {
  a:int;
  b:int;
  extra:string;
}

root_type with_flatten;
)fbs");
}

TEST_CASE(attr_with_rename) {
    EXPECT_EQ(fbs::render(type_info_of<with_rename>()),
              R"fbs(table with_rename {
  my_field:int;
  y:string;
}

root_type with_rename;
)fbs");
}

TEST_CASE(attr_all_optional) {
    EXPECT_EQ(fbs::render(type_info_of<all_optional>()),
              R"fbs(table all_optional {
  a:int;
  b:string;
}

root_type all_optional;
)fbs");
}

TEST_CASE(attr_all_default) {
    EXPECT_EQ(fbs::render(type_info_of<all_default>()),
              R"fbs(table all_default {
  x:int;
  y:string;
}

root_type all_default;
)fbs");
}

TEST_CASE(attr_skip_default) {
    EXPECT_EQ(fbs::render(type_info_of<skip_default>()),
              R"fbs(table skip_default {
  name:string;
  count:int;
}

root_type skip_default;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 5: Flatten combos
// ---------------------------------------------------------------------------

TEST_CASE(flatten_opt) {
    EXPECT_EQ(fbs::render(type_info_of<flatten_opt>()),
              R"fbs(table flatten_opt {
  x:int;
  y:int;
  tag:string;
}

root_type flatten_opt;
)fbs");
}

TEST_CASE(flatten_rename) {
    EXPECT_EQ(fbs::render(type_info_of<flatten_rename>()),
              R"fbs(table flatten_rename {
  alpha:int;
  b:int;
  extra:string;
}

root_type flatten_rename;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 6: Fallback types (pair/tuple/variant all fall back to "string")
// ---------------------------------------------------------------------------

TEST_CASE(fallback_pair) {
    EXPECT_EQ(fbs::render(type_info_of<s_pair>()),
              R"fbs(table s_pair {
  v:string;
}

root_type s_pair;
)fbs");
}

TEST_CASE(fallback_tuple) {
    EXPECT_EQ(fbs::render(type_info_of<s_tuple>()),
              R"fbs(table s_tuple {
  v:string;
}

root_type s_tuple;
)fbs");
}

TEST_CASE(fallback_variant) {
    EXPECT_EQ(fbs::render(type_info_of<var_none>()),
              R"fbs(table var_none {
  v:string;
}

root_type var_none;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 7: Containers
// ---------------------------------------------------------------------------

TEST_CASE(container_vec_i32) {
    EXPECT_EQ(fbs::render(type_info_of<s_vec_i32>()),
              R"fbs(table s_vec_i32 {
  v:[int];
}

root_type s_vec_i32;
)fbs");
}

TEST_CASE(container_set_i32) {
    EXPECT_EQ(fbs::render(type_info_of<s_set_i32>()),
              R"fbs(table s_set_i32 {
  v:[int];
}

root_type s_set_i32;
)fbs");
}

TEST_CASE(container_map_str_i32) {
    EXPECT_EQ(fbs::render(type_info_of<s_map_str_i32>()),
              R"fbs(table s_map_str_i32_vEntry {
  key:string (key);
  value:int;
}

table s_map_str_i32 {
  v:[s_map_str_i32_vEntry];
}

root_type s_map_str_i32;
)fbs");
}

TEST_CASE(container_vec_vec_i32) {
    EXPECT_EQ(fbs::render(type_info_of<s_vec_vec_i32>()),
              R"fbs(table s_vec_vec_i32 {
  v:string;
}

root_type s_vec_vec_i32;
)fbs");
}

TEST_CASE(container_map_str_vec_i32) {
    EXPECT_EQ(fbs::render(type_info_of<s_map_str_vec_i32>()),
              R"fbs(table s_map_str_vec_i32_vEntry {
  key:string (key);
  value:[int];
}

table s_map_str_vec_i32 {
  v:[s_map_str_vec_i32_vEntry];
}

root_type s_map_str_vec_i32;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 8: More containers
// ---------------------------------------------------------------------------

TEST_CASE(container_map_str_struct) {
    EXPECT_EQ(fbs::render(type_info_of<map_str_struct>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

table map_str_struct_entriesEntry {
  key:string (key);
  value:point2d;
}

table map_str_struct {
  entries:[map_str_struct_entriesEntry];
}

root_type map_str_struct;
)fbs");
}

TEST_CASE(container_map_str_enum) {
    EXPECT_EQ(fbs::render(type_info_of<map_str_enum>()),
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table map_str_enum_entriesEntry {
  key:string (key);
  value:color_i8;
}

table map_str_enum {
  entries:[map_str_enum_entriesEntry];
}

root_type map_str_enum;
)fbs");
}

TEST_CASE(container_vec_optional) {
    EXPECT_EQ(fbs::render(type_info_of<vec_optional>()),
              R"fbs(table vec_optional {
  v:[int];
}

root_type vec_optional;
)fbs");
}

TEST_CASE(container_optional_vec) {
    EXPECT_EQ(fbs::render(type_info_of<optional_vec>()),
              R"fbs(table optional_vec {
  v:[int];
}

root_type optional_vec;
)fbs");
}

TEST_CASE(container_with_pair_field) {
    EXPECT_EQ(fbs::render(type_info_of<with_pair_field>()),
              R"fbs(table with_pair_field {
  p:string;
  name:string;
}

root_type with_pair_field;
)fbs");
}

TEST_CASE(container_with_tuple_field) {
    EXPECT_EQ(fbs::render(type_info_of<with_tuple_field>()),
              R"fbs(table with_tuple_field {
  t:string;
  name:string;
}

root_type with_tuple_field;
)fbs");
}

TEST_CASE(container_vec_enum) {
    EXPECT_EQ(fbs::render(type_info_of<vec_enum>()),
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table vec_enum {
  colors:[color_i8];
}

root_type vec_enum;
)fbs");
}

TEST_CASE(container_set_string) {
    EXPECT_EQ(fbs::render(type_info_of<set_string>()),
              R"fbs(table set_string {
  tags:[string];
}

root_type set_string;
)fbs");
}

TEST_CASE(container_vec_map) {
    EXPECT_EQ(fbs::render(type_info_of<vec_map>()),
              R"fbs(table vec_map {
  items:[string];
}

root_type vec_map;
)fbs");
}

TEST_CASE(container_map_vec_struct) {
    EXPECT_EQ(fbs::render(type_info_of<map_vec_struct>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

table map_vec_struct_groupsEntry {
  key:string (key);
  value:[point2d];
}

table map_vec_struct {
  groups:[map_vec_struct_groupsEntry];
}

root_type map_vec_struct;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 9: Nested structs
// ---------------------------------------------------------------------------

TEST_CASE(nested_inner) {
    EXPECT_EQ(fbs::render(type_info_of<inner>()),
              R"fbs(struct inner {
  a:int;
}

root_type inner;
)fbs");
}

TEST_CASE(nested_middle) {
    EXPECT_EQ(fbs::render(type_info_of<middle>()),
              R"fbs(struct inner {
  a:int;
}

table middle {
  i:inner;
  s:string;
}

root_type middle;
)fbs");
}

TEST_CASE(nested_outer) {
    EXPECT_EQ(fbs::render(type_info_of<outer>()),
              R"fbs(struct inner {
  a:int;
}

table middle {
  i:inner;
  s:string;
}

table outer {
  m:middle;
  n:int;
}

root_type outer;
)fbs");
}

TEST_CASE(nested_deep_outer) {
    EXPECT_EQ(fbs::render(type_info_of<deep_outer>()),
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

struct deep_inner {
  c:color_i8;
  v:int;
}

table deep_middle {
  di:deep_inner;
  s:string;
}

table deep_outer {
  dm:deep_middle;
  n:int;
}

root_type deep_outer;
)fbs");
}

TEST_CASE(nested_trivial) {
    EXPECT_EQ(fbs::render(type_info_of<trivial_nested>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

struct trivial_nested {
  p:point2d;
  z:int;
}

root_type trivial_nested;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 10: Optional / pointer
// ---------------------------------------------------------------------------

TEST_CASE(optional_with_optional) {
    EXPECT_EQ(fbs::render(type_info_of<with_optional>()),
              R"fbs(table with_optional {
  name:string;
  age:int;
}

root_type with_optional;
)fbs");
}

TEST_CASE(optional_with_unique) {
    EXPECT_EQ(fbs::render(type_info_of<with_unique>()),
              R"fbs(table with_unique {
  name:string;
  ptr:int;
}

root_type with_unique;
)fbs");
}

TEST_CASE(optional_with_shared) {
    EXPECT_EQ(fbs::render(type_info_of<with_shared>()),
              R"fbs(table with_shared {
  name:string;
  ptr:int;
}

root_type with_shared;
)fbs");
}

TEST_CASE(optional_with_all_ptr) {
    EXPECT_EQ(fbs::render(type_info_of<with_all_ptr>()),
              R"fbs(table with_all_ptr {
  opt:string;
  uniq:int;
  shr:bool;
}

root_type with_all_ptr;
)fbs");
}

TEST_CASE(optional_shared_struct) {
    EXPECT_EQ(fbs::render(type_info_of<shared_struct>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

table shared_struct {
  name:string;
  point:point2d;
}

root_type shared_struct;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 11: More struct combos
// ---------------------------------------------------------------------------

TEST_CASE(combo_optional_struct) {
    EXPECT_EQ(fbs::render(type_info_of<optional_struct>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

table optional_struct {
  point:point2d;
  name:string;
}

root_type optional_struct;
)fbs");
}

TEST_CASE(combo_optional_inner) {
    EXPECT_EQ(fbs::render(type_info_of<optional_inner>()),
              R"fbs(struct inner {
  a:int;
}

table optional_inner {
  i:inner;
  name:string;
}

root_type optional_inner;
)fbs");
}

TEST_CASE(combo_multi_ref) {
    EXPECT_EQ(fbs::render(type_info_of<multi_ref>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

table multi_ref {
  a:point2d;
  b:point2d;
  list:[point2d];
}

root_type multi_ref;
)fbs");
}

TEST_CASE(combo_vec_variant) {
    EXPECT_EQ(fbs::render(type_info_of<vec_variant>()),
              R"fbs(table vec_variant {
  items:[string];
}

root_type vec_variant;
)fbs");
}

TEST_CASE(combo_deep_container) {
    EXPECT_EQ(fbs::render(type_info_of<deep_container>()),
              R"fbs(table deep_container_dataEntry {
  key:string (key);
  value:[string];
}

table deep_container {
  data:[deep_container_dataEntry];
}

root_type deep_container;
)fbs");
}

TEST_CASE(combo_map_of_map) {
    EXPECT_EQ(fbs::render(type_info_of<map_of_map>()),
              R"fbs(table map_of_map_mEntry {
  key:string (key);
  value:string;
}

table map_of_map {
  m:[map_of_map_mEntry];
}

root_type map_of_map;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 12: Combinations
// ---------------------------------------------------------------------------

TEST_CASE(combo_mixed_fields) {
    EXPECT_EQ(fbs::render(type_info_of<combo>()),
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table combo_attrsEntry {
  key:string (key);
  value:int;
}

table combo {
  color:color_i8;
  label:string;
  values:[int];
  attrs:[combo_attrsEntry];
}

root_type combo;
)fbs");
}

TEST_CASE(combo_nested_combo) {
    EXPECT_EQ(fbs::render(type_info_of<nested_combo>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table nested_combo_named_pointsEntry {
  key:string (key);
  value:point2d;
}

table nested_combo {
  point:point2d;
  color:color_i8;
  points:[point2d];
  named_points:[nested_combo_named_pointsEntry];
}

root_type nested_combo;
)fbs");
}

TEST_CASE(combo_multi_map) {
    EXPECT_EQ(fbs::render(type_info_of<multi_map>()),
              R"fbs(table multi_map_aEntry {
  key:string (key);
  value:int;
}

table multi_map_bEntry {
  key:string (key);
  value:string;
}

table multi_map {
  a:[multi_map_aEntry];
  b:[multi_map_bEntry];
}

root_type multi_map;
)fbs");
}

TEST_CASE(combo_vec_of_struct) {
    EXPECT_EQ(fbs::render(type_info_of<vec_of_struct>()),
              R"fbs(struct point2d {
  x:int;
  y:int;
}

table vec_of_struct {
  items:[point2d];
}

root_type vec_of_struct;
)fbs");
}

TEST_CASE(combo_many_fields) {
    EXPECT_EQ(fbs::render(type_info_of<many_fields>()),
              R"fbs(table many_fields {
  a:int;
  b:int;
  c:int;
  d:string;
  e:bool;
  f:double;
}

root_type many_fields;
)fbs");
}

TEST_CASE(combo_set_of_struct) {
    EXPECT_EQ(fbs::render(type_info_of<set_of_struct>()),
              R"fbs(table set_of_struct {
  ids:[int];
  name:string;
}

root_type set_of_struct;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 13: normalize_identifier
// ---------------------------------------------------------------------------

TEST_CASE(normalize_empty) {
    EXPECT_EQ(kota::naming::normalize_identifier(""), "unnamed");
}

TEST_CASE(normalize_leading_digit) {
    EXPECT_EQ(kota::naming::normalize_identifier("3abc"), "_3abc");
}

TEST_CASE(normalize_special_chars) {
    EXPECT_EQ(kota::naming::normalize_identifier("a::b<c>"), "a__b_c_");
}

TEST_CASE(normalize_alpha_unchanged) {
    EXPECT_EQ(kota::naming::normalize_identifier("FooBar"), "FooBar");
}

TEST_CASE(normalize_underscores_preserved) {
    EXPECT_EQ(kota::naming::normalize_identifier("__hello__"), "__hello__");
}

TEST_CASE(normalize_spaces_to_underscores) {
    EXPECT_EQ(kota::naming::normalize_identifier("a b c"), "a_b_c");
}

TEST_CASE(normalize_single_char) {
    EXPECT_EQ(kota::naming::normalize_identifier("x"), "x");
}

TEST_CASE(normalize_all_digits) {
    EXPECT_EQ(kota::naming::normalize_identifier("123"), "_123");
}

// ---------------------------------------------------------------------------
// Group 14: map_entry_identifier
// ---------------------------------------------------------------------------

TEST_CASE(map_entry_simple) {
    EXPECT_EQ(fbs::map_entry_identifier("Foo", "bar"), "Foo_barEntry");
}

TEST_CASE(map_entry_special_chars) {
    EXPECT_EQ(fbs::map_entry_identifier("a::b", "c<d>"), "a__b_c_d_Entry");
}

// ---------------------------------------------------------------------------
// Group 15: Verification — re-render the same type to ensure emitter state
// is fully reset between calls (no leaking of emitted_objects / emitted_enums
// across independent render() invocations).
// ---------------------------------------------------------------------------

TEST_CASE(re_render_point2d) {
    // First call
    auto first = fbs::render(type_info_of<point2d>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<point2d>());
    EXPECT_EQ(first,
              R"fbs(struct point2d {
  x:int;
  y:int;
}

root_type point2d;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_with_enum) {
    // First call
    auto first = fbs::render(type_info_of<with_enum>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<with_enum>());
    EXPECT_EQ(first,
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table with_enum {
  c:color_i8;
  name:string;
}

root_type with_enum;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_nested_outer) {
    // First call
    auto first = fbs::render(type_info_of<outer>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<outer>());
    EXPECT_EQ(first,
              R"fbs(struct inner {
  a:int;
}

table middle {
  i:inner;
  s:string;
}

table outer {
  m:middle;
  n:int;
}

root_type outer;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_combo) {
    // First call
    auto first = fbs::render(type_info_of<combo>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<combo>());
    EXPECT_EQ(first,
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table combo_attrsEntry {
  key:string (key);
  value:int;
}

table combo {
  color:color_i8;
  label:string;
  values:[int];
  attrs:[combo_attrsEntry];
}

root_type combo;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_map_str_i32) {
    // First call
    auto first = fbs::render(type_info_of<s_map_str_i32>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<s_map_str_i32>());
    EXPECT_EQ(first,
              R"fbs(table s_map_str_i32_vEntry {
  key:string (key);
  value:int;
}

table s_map_str_i32 {
  v:[s_map_str_i32_vEntry];
}

root_type s_map_str_i32;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_deep_outer) {
    // First call
    auto first = fbs::render(type_info_of<deep_outer>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<deep_outer>());
    EXPECT_EQ(first,
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

struct deep_inner {
  c:color_i8;
  v:int;
}

table deep_middle {
  di:deep_inner;
  s:string;
}

table deep_outer {
  dm:deep_middle;
  n:int;
}

root_type deep_outer;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_nested_combo) {
    // First call
    auto first = fbs::render(type_info_of<nested_combo>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<nested_combo>());
    EXPECT_EQ(first,
              R"fbs(struct point2d {
  x:int;
  y:int;
}

enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table nested_combo_named_pointsEntry {
  key:string (key);
  value:point2d;
}

table nested_combo {
  point:point2d;
  color:color_i8;
  points:[point2d];
  named_points:[nested_combo_named_pointsEntry];
}

root_type nested_combo;
)fbs");
    EXPECT_EQ(first, second);
}

TEST_CASE(re_render_multi_map) {
    // First call
    auto first = fbs::render(type_info_of<multi_map>());
    // Second call — must produce identical output
    auto second = fbs::render(type_info_of<multi_map>());
    EXPECT_EQ(first,
              R"fbs(table multi_map_aEntry {
  key:string (key);
  value:int;
}

table multi_map_bEntry {
  key:string (key);
  value:string;
}

table multi_map {
  a:[multi_map_aEntry];
  b:[multi_map_bEntry];
}

root_type multi_map;
)fbs");
    EXPECT_EQ(first, second);
}

// ---------------------------------------------------------------------------
// Group 16: Cross-type rendering — rendering different types in sequence
// does not interfere with each other (emitter is fully stateless across calls).
// ---------------------------------------------------------------------------

TEST_CASE(cross_type_scalar_then_enum) {
    // Render a scalar struct first
    auto scalar_result = fbs::render(type_info_of<s_i32>());
    EXPECT_EQ(scalar_result,
              R"fbs(struct s_i32 {
  v:int;
}

root_type s_i32;
)fbs");

    // Then render an enum-containing struct — enum must still appear
    auto enum_result = fbs::render(type_info_of<with_enum>());
    EXPECT_EQ(enum_result,
              R"fbs(enum color_i8:byte {
  red = 0,
  green = 1,
  blue = 2
}

table with_enum {
  c:color_i8;
  name:string;
}

root_type with_enum;
)fbs");
}

TEST_CASE(cross_type_nested_then_flat) {
    // Render a deeply nested struct first
    auto nested_result = fbs::render(type_info_of<outer>());
    EXPECT_EQ(nested_result,
              R"fbs(struct inner {
  a:int;
}

table middle {
  i:inner;
  s:string;
}

table outer {
  m:middle;
  n:int;
}

root_type outer;
)fbs");

    // Then render a flat struct — must not include leftover nested types
    auto flat_result = fbs::render(type_info_of<single_field>());
    EXPECT_EQ(flat_result,
              R"fbs(struct single_field {
  x:int;
}

root_type single_field;
)fbs");
}

TEST_CASE(cross_type_map_then_vec) {
    // Render a map-containing struct first
    auto map_result = fbs::render(type_info_of<s_map_str_i32>());
    EXPECT_EQ(map_result,
              R"fbs(table s_map_str_i32_vEntry {
  key:string (key);
  value:int;
}

table s_map_str_i32 {
  v:[s_map_str_i32_vEntry];
}

root_type s_map_str_i32;
)fbs");

    // Then render a vector-containing struct — must not include leftover entries
    auto vec_result = fbs::render(type_info_of<s_vec_i32>());
    EXPECT_EQ(vec_result,
              R"fbs(table s_vec_i32 {
  v:[int];
}

root_type s_vec_i32;
)fbs");
}

TEST_CASE(cross_type_flatten_then_rename) {
    // Render a flatten struct first
    auto flatten_result = fbs::render(type_info_of<with_flatten>());
    EXPECT_EQ(flatten_result,
              R"fbs(table with_flatten {
  a:int;
  b:int;
  extra:string;
}

root_type with_flatten;
)fbs");

    // Then render a rename struct — must apply rename independently
    auto rename_result = fbs::render(type_info_of<with_rename>());
    EXPECT_EQ(rename_result,
              R"fbs(table with_rename {
  my_field:int;
  y:string;
}

root_type with_rename;
)fbs");
}

TEST_CASE(cross_type_skip_then_default) {
    // Render a skip struct first
    auto skip_result = fbs::render(type_info_of<with_skip>());
    EXPECT_EQ(skip_result,
              R"fbs(table with_skip {
  visible:string;
}

root_type with_skip;
)fbs");

    // Then render a default struct — hidden field must not leak
    auto default_result = fbs::render(type_info_of<with_default>());
    EXPECT_EQ(default_result,
              R"fbs(table with_default {
  name:string;
  count:int;
}

root_type with_default;
)fbs");
}

// ---------------------------------------------------------------------------
// Group 17: Output string properties — verify structural invariants
// of the rendered FBS text (trailing newline, root_type presence, etc.).
// ---------------------------------------------------------------------------

TEST_CASE(output_ends_with_newline) {
    // Every rendered schema must end with exactly one newline
    auto result = fbs::render(type_info_of<point2d>());
    EXPECT_EQ(result.back(), '\n');
}

TEST_CASE(output_contains_root_type_for_scalar) {
    auto result = fbs::render(type_info_of<s_bool>());
    EXPECT_EQ(result,
              R"fbs(struct s_bool {
  v:bool;
}

root_type s_bool;
)fbs");
}

TEST_CASE(output_contains_root_type_for_table) {
    auto result = fbs::render(type_info_of<with_string>());
    EXPECT_EQ(result,
              R"fbs(table with_string {
  name:string;
  value:int;
}

root_type with_string;
)fbs");
}

TEST_CASE(output_contains_root_type_for_nested) {
    auto result = fbs::render(type_info_of<middle>());
    EXPECT_EQ(result,
              R"fbs(struct inner {
  a:int;
}

table middle {
  i:inner;
  s:string;
}

root_type middle;
)fbs");
}

TEST_CASE(output_contains_root_type_for_enum_struct) {
    auto result = fbs::render(type_info_of<with_level>());
    EXPECT_EQ(result,
              R"fbs(enum level_i16:short {
  low = 0,
  mid = 50,
  high = 100
}

struct with_level {
  l:level_i16;
  v:int;
}

root_type with_level;
)fbs");
}

TEST_CASE(output_contains_root_type_for_map) {
    auto result = fbs::render(type_info_of<multi_map>());
    EXPECT_EQ(result,
              R"fbs(table multi_map_aEntry {
  key:string (key);
  value:int;
}

table multi_map_bEntry {
  key:string (key);
  value:string;
}

table multi_map {
  a:[multi_map_aEntry];
  b:[multi_map_bEntry];
}

root_type multi_map;
)fbs");
}

TEST_CASE(output_rejects_non_struct_root) {
    auto result = fbs::render(type_info_of<std::int32_t>());
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// Group 18: Additional normalize_identifier edge cases
// ---------------------------------------------------------------------------

TEST_CASE(normalize_tab_char) {
    // Tab is not alphanumeric, should become underscore
    EXPECT_EQ(kota::naming::normalize_identifier("a\tb"), "a_b");
}

TEST_CASE(normalize_mixed_symbols) {
    // Multiple different non-alphanumeric characters
    EXPECT_EQ(kota::naming::normalize_identifier("a+b-c*d/e"), "a_b_c_d_e");
}

TEST_CASE(normalize_dot_separator) {
    // Dots should become underscores
    EXPECT_EQ(kota::naming::normalize_identifier("com.example.Foo"), "com_example_Foo");
}

TEST_CASE(normalize_angle_brackets) {
    // Template-like syntax
    EXPECT_EQ(kota::naming::normalize_identifier("vector<int>"), "vector_int_");
}

TEST_CASE(normalize_parens) {
    // Parentheses
    EXPECT_EQ(kota::naming::normalize_identifier("foo(bar)"), "foo_bar_");
}

TEST_CASE(normalize_single_special) {
    // Single non-alphanumeric character
    EXPECT_EQ(kota::naming::normalize_identifier("@"), "_");
}

TEST_CASE(normalize_leading_underscore) {
    // Leading underscore is fine — no digit prefix
    EXPECT_EQ(kota::naming::normalize_identifier("_foo"), "_foo");
}

TEST_CASE(normalize_trailing_digit) {
    // Trailing digit is fine
    EXPECT_EQ(kota::naming::normalize_identifier("abc123"), "abc123");
}

TEST_CASE(normalize_all_underscores) {
    // All underscores — should pass through unchanged
    EXPECT_EQ(kota::naming::normalize_identifier("___"), "___");
}

TEST_CASE(normalize_namespace_colons) {
    // Double colon namespace separator
    EXPECT_EQ(kota::naming::normalize_identifier("std::string"), "std__string");
}

// ---------------------------------------------------------------------------
// Group 19: Additional map_entry_identifier edge cases
// ---------------------------------------------------------------------------

TEST_CASE(map_entry_empty_field) {
    // Empty field name — should produce "Owner_Entry"
    EXPECT_EQ(fbs::map_entry_identifier("Owner", ""), "Owner_Entry");
}

TEST_CASE(map_entry_numeric_owner) {
    // Owner starts with digit after normalization
    EXPECT_EQ(fbs::map_entry_identifier("123Type", "field"), "_123Type_fieldEntry");
}

TEST_CASE(map_entry_underscored_names) {
    // Names with existing underscores
    EXPECT_EQ(fbs::map_entry_identifier("my_struct", "my_field"), "my_struct_my_fieldEntry");
}

TEST_CASE(map_entry_template_names) {
    // Template-like type name
    EXPECT_EQ(fbs::map_entry_identifier("Map<K,V>", "data"), "Map_K_V__dataEntry");
}

};  // TEST_SUITE(serde_fbs_schema)

}  // namespace

}  // namespace kota::meta
