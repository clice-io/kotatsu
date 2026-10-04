#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/compare.h"

namespace kota::meta {

namespace {

struct c_point {
    int x;
    int y;
};

struct c_box {
    c_point pos;
    int id;
};

struct with_custom_ops {
    int x;
    int y;

    bool operator==(const with_custom_ops& other) const {
        return y == other.y;
    }

    bool operator<(const with_custom_ops& other) const {
        return y < other.y;
    }
};

struct c_point_hash {
    std::size_t operator()(const c_point& p) const {
        return (static_cast<std::size_t>(p.x) << 32) ^ static_cast<std::size_t>(p.y);
    }
};

struct c_point_equal {
    bool operator()(const c_point& lhs, const c_point& rhs) const {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }
};

struct with_custom_ops_hash {
    std::size_t operator()(const with_custom_ops& p) const {
        return std::hash<int>{}(p.y);
    }
};

struct with_custom_ops_equal {
    bool operator()(const with_custom_ops& lhs, const with_custom_ops& rhs) const {
        return lhs.y == rhs.y;
    }
};

template <typename T>
struct custom_sequence {
    std::vector<T> data;

    auto begin() {
        return data.begin();
    }

    auto end() {
        return data.end();
    }

    auto begin() const {
        return data.begin();
    }

    auto end() const {
        return data.end();
    }

    std::size_t size() const {
        return data.size();
    }
};

template <typename T>
custom_sequence<T> make_custom_sequence(std::initializer_list<T> init) {
    return custom_sequence<T>{std::vector<T>(init)};
}

template <typename K,
          typename V,
          typename Hash = std::hash<K>,
          typename KeyEqual = std::equal_to<K>>
struct unsized_unordered_map {
    using key_type = K;
    using mapped_type = V;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using storage_type = std::unordered_map<key_type, mapped_type, hasher, key_equal>;

    unsized_unordered_map() = default;

    unsized_unordered_map(std::initializer_list<typename storage_type::value_type> init) :
        data(init) {}

    auto begin() {
        return data.begin();
    }

    auto end() {
        return data.end();
    }

    auto begin() const {
        return data.begin();
    }

    auto end() const {
        return data.end();
    }

    auto find(const key_type& key) {
        return data.find(key);
    }

    auto find(const key_type& key) const {
        return data.find(key);
    }

    storage_type data;
};

template <typename T, typename Hash = std::hash<T>, typename KeyEqual = std::equal_to<T>>
struct unsized_unordered_set {
    using key_type = T;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using storage_type = std::unordered_set<key_type, hasher, key_equal>;

    unsized_unordered_set() = default;

    unsized_unordered_set(std::initializer_list<key_type> init) : data(init) {}

    auto begin() {
        return data.begin();
    }

    auto end() {
        return data.end();
    }

    auto begin() const {
        return data.begin();
    }

    auto end() const {
        return data.end();
    }

    auto find(const key_type& key) {
        return data.find(key);
    }

    auto find(const key_type& key) const {
        return data.find(key);
    }

    storage_type data;
};

static_assert(!std::ranges::sized_range<const unsized_unordered_map<int, int>>);
static_assert(!std::ranges::sized_range<const unsized_unordered_set<int>>);

ZEST_SUITE(meta_compare) {

ZEST_CASE(primitive_types) {
    ZEXPECT(eq(7, 7));
    ZEXPECT(ne(7, 8));
    ZEXPECT(lt(7, 8));
    ZEXPECT(le(7, 7));
    ZEXPECT(gt(9, 8));
    ZEXPECT(ge(9, 9));
}

ZEST_CASE(mixed_sign_integers_compare_by_value) {
    constexpr unsigned max = std::numeric_limits<unsigned>::max();
    ZEXPECT(lt(-1, 0u));
    ZEXPECT(le(-1, 0u));
    ZEXPECT(gt(0u, -1));
    ZEXPECT(ge(0u, -1));
    ZEXPECT(ne(-1, max));
    ZEXPECT(!eq(-1, max));
}

// char is signed on some targets and unsigned on others; either way its
// minimum is less than 1.
enum char_backed : char { char_min = std::numeric_limits<char>::min() };

ZEST_CASE(char_backed_enums_compare_by_value) {
    ZEXPECT(lt(char_min, 1u));
    ZEXPECT(le(char_min, 1u));
    ZEXPECT(gt(1u, char_min));
    ZEXPECT(ge(1u, char_min));
    ZEXPECT(ne(char_min, std::numeric_limits<unsigned>::max()));
}

ZEST_CASE(string_native) {
    constexpr std::string_view view = "kotatsu";
    constexpr char literal[] = "kotatsu";
    const std::string str = "kotatsu";

    ZEXPECT(eq(view, literal));
    ZEXPECT(eq(literal, view));
    ZEXPECT(eq(str, literal));
    ZEXPECT(eq(literal, str));
    ZEXPECT(!ne(view, literal));
}

ZEST_CASE(struct_recursive) {
    c_box a{
        .pos = {.x = 1, .y = 2},
        .id = 10,
    };
    c_box b{
        .pos = {.x = 1, .y = 2},
        .id = 10,
    };
    c_box c{
        .pos = {.x = 1, .y = 3},
        .id = 1,
    };
    c_box d{
        .pos = {.x = 2, .y = 0},
        .id = 0,
    };

    ZEXPECT(eq(a, b));
    ZEXPECT(!ne(a, b));
    ZEXPECT(le(a, b));
    ZEXPECT(ge(a, b));
    ZEXPECT(!eq(a, c));
    ZEXPECT(ne(a, c));
    ZEXPECT(lt(a, c));
    ZEXPECT(le(a, c));
    ZEXPECT(!gt(a, c));
    ZEXPECT(!ge(a, c));
    ZEXPECT(gt(c, a));
    ZEXPECT(ge(c, a));
    ZEXPECT(lt(c, d));
    ZEXPECT(gt(d, c));
}

ZEST_CASE(vector_nested) {
    std::vector<c_point> a{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3},
    };
    std::vector<c_point> b{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3},
    };
    std::vector<c_point> c{
        {.x = 1, .y = 2},
        {.x = 2, .y = 4},
    };

    ZEXPECT(eq(a, b));
    ZEXPECT(!ne(a, b));
    ZEXPECT(!lt(a, b));
    ZEXPECT(!gt(a, b));
    ZEXPECT(le(a, b));
    ZEXPECT(ge(a, b));

    ZEXPECT(!eq(a, c));
    ZEXPECT(ne(a, c));
    ZEXPECT(lt(a, c));
    ZEXPECT(le(a, c));
    ZEXPECT(!gt(a, c));
    ZEXPECT(!ge(a, c));

    ZEXPECT(gt(c, a));
    ZEXPECT(ge(c, a));

    std::vector<std::vector<c_point>> nested_a{a, {{.x = 3, .y = 1}}};
    std::vector<std::vector<c_point>> nested_b{b, {{.x = 3, .y = 1}}};
    std::vector<std::vector<c_point>> nested_c{b, {{.x = 3, .y = 2}}};

    ZEXPECT(eq(nested_a, nested_b));
    ZEXPECT(!ne(nested_a, nested_b));
    ZEXPECT(le(nested_a, nested_b));
    ZEXPECT(ge(nested_a, nested_b));

    ZEXPECT(!eq(nested_a, nested_c));
    ZEXPECT(ne(nested_a, nested_c));
    ZEXPECT(lt(nested_a, nested_c));
    ZEXPECT(le(nested_a, nested_c));
    ZEXPECT(!gt(nested_a, nested_c));
    ZEXPECT(!ge(nested_a, nested_c));

    ZEXPECT(gt(nested_c, nested_a));
    ZEXPECT(ge(nested_c, nested_a));
}

ZEST_CASE(vector_custom) {
    std::vector<with_custom_ops> a{
        {.x = 100, .y = 1},
        {.x = 200, .y = 2}
    };
    std::vector<with_custom_ops> b{
        {.x = 0,   .y = 1},
        {.x = 999, .y = 2}
    };
    std::vector<with_custom_ops> c{
        {.x = 0,   .y = 1},
        {.x = 999, .y = 3}
    };

    // If reflection fallback were used, `eq(a, b)` would be false because x differs.
    ZEXPECT(eq(a, b));
    ZEXPECT(!ne(a, b));
    ZEXPECT(le(a, b));
    ZEXPECT(ge(a, b));

    ZEXPECT(!eq(a, c));
    ZEXPECT(ne(a, c));
    ZEXPECT(lt(a, c));
    ZEXPECT(le(a, c));
    ZEXPECT(!gt(a, c));
    ZEXPECT(!ge(a, c));

    ZEXPECT(gt(c, a));
    ZEXPECT(ge(c, a));
}

ZEST_CASE(set_mixed) {
    std::set<c_point, lt_t> no_ops_a{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3}
    };
    std::set<c_point, lt_t> no_ops_b{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3}
    };
    std::set<c_point, lt_t> no_ops_c{
        {.x = 1, .y = 2},
        {.x = 2, .y = 4}
    };

    ZEXPECT(eq(no_ops_a, no_ops_b));
    ZEXPECT(!ne(no_ops_a, no_ops_b));
    ZEXPECT(le(no_ops_a, no_ops_b));
    ZEXPECT(ge(no_ops_a, no_ops_b));

    ZEXPECT(!eq(no_ops_a, no_ops_c));
    ZEXPECT(ne(no_ops_a, no_ops_c));
    ZEXPECT(lt(no_ops_a, no_ops_c));
    ZEXPECT(le(no_ops_a, no_ops_c));
    ZEXPECT(!gt(no_ops_a, no_ops_c));
    ZEXPECT(!ge(no_ops_a, no_ops_c));

    ZEXPECT(gt(no_ops_c, no_ops_a));
    ZEXPECT(ge(no_ops_c, no_ops_a));

    std::set<with_custom_ops> ops_a{
        {.x = 100, .y = 1},
        {.x = 200, .y = 2}
    };
    std::set<with_custom_ops> ops_b{
        {.x = 0,   .y = 1},
        {.x = 999, .y = 2}
    };
    std::set<with_custom_ops> ops_c{
        {.x = 0,   .y = 1},
        {.x = 999, .y = 3}
    };

    ZEXPECT(eq(ops_a, ops_b));
    ZEXPECT(!ne(ops_a, ops_b));
    ZEXPECT(le(ops_a, ops_b));
    ZEXPECT(ge(ops_a, ops_b));

    ZEXPECT(!eq(ops_a, ops_c));
    ZEXPECT(ne(ops_a, ops_c));
    ZEXPECT(lt(ops_a, ops_c));
    ZEXPECT(le(ops_a, ops_c));
    ZEXPECT(!gt(ops_a, ops_c));
    ZEXPECT(!ge(ops_a, ops_c));

    ZEXPECT(gt(ops_c, ops_a));
    ZEXPECT(ge(ops_c, ops_a));
}

ZEST_CASE(uset_mixed) {
    std::unordered_set<c_point, c_point_hash, c_point_equal> no_ops_a{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3},
    };
    std::unordered_set<c_point, c_point_hash, c_point_equal> no_ops_b{
        {.x = 2, .y = 3},
        {.x = 1, .y = 2},
    };
    std::unordered_set<c_point, c_point_hash, c_point_equal> no_ops_c{
        {.x = 1, .y = 2},
        {.x = 2, .y = 4},
    };

    ZEXPECT(eq(no_ops_a, no_ops_b));
    ZEXPECT(!ne(no_ops_a, no_ops_b));
    ZEXPECT(!eq(no_ops_a, no_ops_c));
    ZEXPECT(ne(no_ops_a, no_ops_c));

    std::unordered_set<with_custom_ops, with_custom_ops_hash, with_custom_ops_equal> ops_a{
        {.x = 100, .y = 1},
        {.x = 200, .y = 2},
    };
    std::unordered_set<with_custom_ops, with_custom_ops_hash, with_custom_ops_equal> ops_b{
        {.x = 0,   .y = 1},
        {.x = 999, .y = 2},
    };
    std::unordered_set<with_custom_ops, with_custom_ops_hash, with_custom_ops_equal> ops_c{
        {.x = 0,   .y = 1},
        {.x = 999, .y = 3},
    };

    ZEXPECT(eq(ops_a, ops_b));
    ZEXPECT(!ne(ops_a, ops_b));
    ZEXPECT(!eq(ops_a, ops_c));
    ZEXPECT(ne(ops_a, ops_c));
}

ZEST_CASE(uset_unsized_range_regression) {
    using uset_t = unsized_unordered_set<c_point, c_point_hash, c_point_equal>;
    static_assert(set_range<uset_t>);
    static_assert(unordered_associative_range<uset_t>);

    uset_t unsized_small{
        {.x = 1, .y = 2},
    };
    uset_t unsized_large{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3},
    };
    std::unordered_set<c_point, c_point_hash, c_point_equal> sized_small{
        {.x = 1, .y = 2},
    };
    std::unordered_set<c_point, c_point_hash, c_point_equal> sized_large{
        {.x = 1, .y = 2},
        {.x = 2, .y = 3},
    };

    ZEXPECT(!eq(unsized_small, unsized_large));
    ZEXPECT(!eq(unsized_small, sized_large));
    ZEXPECT(!eq(sized_small, unsized_large));
    ZEXPECT(eq(unsized_large, sized_large));
    ZEXPECT(eq(sized_large, unsized_large));
}

ZEST_CASE(map_mixed) {
    std::map<int, c_point> no_ops_a{
        {1, {.x = 1, .y = 2}},
        {2, {.x = 2, .y = 3}}
    };
    std::map<int, c_point> no_ops_b{
        {1, {.x = 1, .y = 2}},
        {2, {.x = 2, .y = 3}}
    };
    std::map<int, c_point> no_ops_c{
        {1, {.x = 1, .y = 2}},
        {2, {.x = 2, .y = 4}}
    };

    ZEXPECT(eq(no_ops_a, no_ops_b));
    ZEXPECT(!ne(no_ops_a, no_ops_b));
    ZEXPECT(le(no_ops_a, no_ops_b));
    ZEXPECT(ge(no_ops_a, no_ops_b));

    ZEXPECT(!eq(no_ops_a, no_ops_c));
    ZEXPECT(ne(no_ops_a, no_ops_c));
    ZEXPECT(lt(no_ops_a, no_ops_c));
    ZEXPECT(le(no_ops_a, no_ops_c));
    ZEXPECT(!gt(no_ops_a, no_ops_c));
    ZEXPECT(!ge(no_ops_a, no_ops_c));

    ZEXPECT(gt(no_ops_c, no_ops_a));
    ZEXPECT(ge(no_ops_c, no_ops_a));

    std::map<int, with_custom_ops> ops_a{
        {1, {.x = 100, .y = 1}},
        {2, {.x = 200, .y = 2}}
    };
    std::map<int, with_custom_ops> ops_b{
        {1, {.x = 0, .y = 1}  },
        {2, {.x = 999, .y = 2}}
    };
    std::map<int, with_custom_ops> ops_c{
        {1, {.x = 0, .y = 1}  },
        {2, {.x = 999, .y = 3}}
    };

    ZEXPECT(eq(ops_a, ops_b));
    ZEXPECT(!ne(ops_a, ops_b));
    ZEXPECT(le(ops_a, ops_b));
    ZEXPECT(ge(ops_a, ops_b));

    ZEXPECT(!eq(ops_a, ops_c));
    ZEXPECT(ne(ops_a, ops_c));
    ZEXPECT(lt(ops_a, ops_c));
    ZEXPECT(le(ops_a, ops_c));
    ZEXPECT(!gt(ops_a, ops_c));
    ZEXPECT(!ge(ops_a, ops_c));

    ZEXPECT(gt(ops_c, ops_a));
    ZEXPECT(ge(ops_c, ops_a));
}

ZEST_CASE(umap_mixed) {
    std::unordered_map<int, c_point> no_ops_a{
        {1, {.x = 1, .y = 2}},
        {2, {.x = 2, .y = 3}}
    };
    std::unordered_map<int, c_point> no_ops_b{
        {2, {.x = 2, .y = 3}},
        {1, {.x = 1, .y = 2}}
    };
    std::unordered_map<int, c_point> no_ops_c{
        {2, {.x = 2, .y = 4}},
        {1, {.x = 1, .y = 2}}
    };

    ZEXPECT(eq(no_ops_a, no_ops_b));
    ZEXPECT(!ne(no_ops_a, no_ops_b));
    ZEXPECT(!eq(no_ops_a, no_ops_c));
    ZEXPECT(ne(no_ops_a, no_ops_c));

    std::unordered_map<int, with_custom_ops> ops_a{
        {1, {.x = 100, .y = 1}},
        {2, {.x = 200, .y = 2}}
    };
    std::unordered_map<int, with_custom_ops> ops_b{
        {2, {.x = 999, .y = 2}},
        {1, {.x = 0, .y = 1}  }
    };
    std::unordered_map<int, with_custom_ops> ops_c{
        {2, {.x = 999, .y = 3}},
        {1, {.x = 0, .y = 1}  }
    };

    ZEXPECT(eq(ops_a, ops_b));
    ZEXPECT(!ne(ops_a, ops_b));
    ZEXPECT(!eq(ops_a, ops_c));
    ZEXPECT(ne(ops_a, ops_c));
}

ZEST_CASE(umap_unsized_range_regression) {
    using umap_t = unsized_unordered_map<int, c_point>;
    static_assert(map_range<umap_t>);
    static_assert(unordered_associative_range<umap_t>);

    umap_t unsized_small{
        {1, {.x = 1, .y = 2}},
    };
    umap_t unsized_large{
        {1, {.x = 1, .y = 2}},
        {2, {.x = 2, .y = 3}},
    };
    std::unordered_map<int, c_point> sized_small{
        {1, {.x = 1, .y = 2}},
    };
    std::unordered_map<int, c_point> sized_large{
        {1, {.x = 1, .y = 2}},
        {2, {.x = 2, .y = 3}},
    };

    ZEXPECT(!eq(unsized_small, unsized_large));
    ZEXPECT(!eq(unsized_small, sized_large));
    ZEXPECT(!eq(sized_small, unsized_large));
    ZEXPECT(eq(unsized_large, sized_large));
    ZEXPECT(eq(sized_large, unsized_large));
}

ZEST_CASE(custom_plain) {
    auto a = make_custom_sequence<c_point>({
        {.x = 1, .y = 2},
        {.x = 2, .y = 3}
    });
    auto b = make_custom_sequence<c_point>({
        {.x = 1, .y = 2},
        {.x = 2, .y = 3}
    });
    auto c = make_custom_sequence<c_point>({
        {.x = 1, .y = 2},
        {.x = 2, .y = 4}
    });

    ZEXPECT(eq(a, b));
    ZEXPECT(!ne(a, b));
    ZEXPECT(le(a, b));
    ZEXPECT(ge(a, b));

    ZEXPECT(!eq(a, c));
    ZEXPECT(ne(a, c));
    ZEXPECT(lt(a, c));
    ZEXPECT(le(a, c));
    ZEXPECT(!gt(a, c));
    ZEXPECT(!ge(a, c));

    ZEXPECT(gt(c, a));
    ZEXPECT(ge(c, a));
}

ZEST_CASE(custom_ops) {
    auto a = make_custom_sequence<with_custom_ops>({
        {.x = 100, .y = 1},
        {.x = 200, .y = 2}
    });
    auto b = make_custom_sequence<with_custom_ops>({
        {.x = 0,   .y = 1},
        {.x = 999, .y = 2}
    });
    auto c = make_custom_sequence<with_custom_ops>({
        {.x = 0,   .y = 1},
        {.x = 999, .y = 3}
    });

    ZEXPECT(eq(a, b));
    ZEXPECT(!ne(a, b));
    ZEXPECT(le(a, b));
    ZEXPECT(ge(a, b));

    ZEXPECT(!eq(a, c));
    ZEXPECT(ne(a, c));
    ZEXPECT(lt(a, c));
    ZEXPECT(le(a, c));
    ZEXPECT(!gt(a, c));
    ZEXPECT(!ge(a, c));

    ZEXPECT(gt(c, a));
    ZEXPECT(ge(c, a));
}

struct v_circle {
    int radius;
};

struct v_rect {
    int width;
    int height;
};

#if KOTA_ENABLE_EXCEPTIONS
struct throwing_alt {
    throwing_alt() = default;

    throwing_alt(const throwing_alt&) {
        throw std::runtime_error("throwing_alt");
    }

    auto operator==(const throwing_alt&) const -> bool {
        return true;
    }

    auto operator<(const throwing_alt&) const -> bool {
        return false;
    }
};
#endif

ZEST_CASE(variant_eq_unreflectable_alt) {
    using shape_t = std::variant<v_circle, v_rect>;
    shape_t a = v_circle{.radius = 3};
    shape_t b = v_circle{.radius = 3};
    shape_t c = v_circle{.radius = 4};
    shape_t d = v_rect{.width = 1, .height = 2};

    ZEXPECT(eq(a, b));
    ZEXPECT(!eq(a, c));
    ZEXPECT(!eq(a, d));
    ZEXPECT(ne(a, c));
}

ZEST_CASE(variant_lt_index_tie_break) {
    using shape_t = std::variant<v_circle, v_rect>;
    shape_t a = v_circle{.radius = 10};
    shape_t b = v_rect{.width = 1, .height = 1};

    ZEXPECT(lt(a, b));
    ZEXPECT(!lt(b, a));
    ZEXPECT(le(a, b));
    ZEXPECT(gt(b, a));
    ZEXPECT(ge(b, a));
}

ZEST_CASE(variant_lt_same_alternative) {
    using shape_t = std::variant<v_circle, v_rect>;
    shape_t small = v_rect{.width = 1, .height = 2};
    shape_t big = v_rect{.width = 1, .height = 5};
    ZEXPECT(lt(small, big));
    ZEXPECT(!lt(big, small));
}

#if KOTA_ENABLE_EXCEPTIONS
ZEST_CASE(variant_valueless_compares_less) {
    using shape_t = std::variant<throwing_alt, v_circle>;
    shape_t valued = v_circle{.radius = 1};
    shape_t valueless;
    try {
        throwing_alt seed;
        valueless.emplace<throwing_alt>(seed);
    } catch(const std::exception&) {}
    ZASSERT(valueless.valueless_by_exception());

    ZEXPECT(lt(valueless, valued));
    ZEXPECT(!lt(valued, valueless));
    ZEXPECT(!eq(valueless, valued));

    shape_t valueless2;
    try {
        throwing_alt seed;
        valueless2.emplace<throwing_alt>(seed);
    } catch(const std::exception&) {}
    ZASSERT(valueless2.valueless_by_exception());
    ZEXPECT(!lt(valueless, valueless2));
    ZEXPECT(!lt(valueless2, valueless));
}
#endif

struct kind_tag {
    constexpr static auto spec = make_struct_spec(dsl::tag = "kind");
};

ZEST_CASE(variant_in_annotation) {
    using tagged_shape_t = annotate<kind_tag>::type<std::variant<v_circle, v_rect>>;
    tagged_shape_t lhs = v_circle{.radius = 2};
    tagged_shape_t rhs = v_circle{.radius = 2};
    tagged_shape_t other = v_rect{.width = 3, .height = 4};
    ZEXPECT(eq(lhs, rhs));
    ZEXPECT(!eq(lhs, other));
    ZEXPECT(lt(lhs, other));
}

ZEST_CASE(functor_sort) {
    std::vector<c_point> values{
        {.x = 2, .y = 1},
        {.x = 1, .y = 4},
        {.x = 1, .y = 2},
        {.x = 1, .y = 3},
    };

    std::ranges::sort(values, lt);

    // zest compares a check's operands with meta's comparisons, so the tests of
    // those check plain bools: a parenthesized check is not split.
    ZASSERT((values.size() == 4U));
    ZEXPECT((values[0].x == 1));
    ZEXPECT((values[0].y == 2));
    ZEXPECT((values[1].x == 1));
    ZEXPECT((values[1].y == 3));
    ZEXPECT((values[2].x == 1));
    ZEXPECT((values[2].y == 4));
    ZEXPECT((values[3].x == 2));
    ZEXPECT((values[3].y == 1));
}

};  // ZEST_SUITE(meta_compare)

}  // namespace

}  // namespace kota::meta
