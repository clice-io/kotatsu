#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <variant>

#ifdef __cpp_exceptions
#include <stdexcept>
#endif

#include "eventide/zest/zest.h"
#include "eventide/common/small_vector.h"

namespace eventide {

namespace {

struct move_only {
    int value = 0;

    move_only() = default;

    explicit move_only(int v) : value(v) {}

    move_only(const move_only&) = delete;
    move_only& operator=(const move_only&) = delete;

    move_only(move_only&& other) noexcept : value(other.value) {
        other.value = -1;
    }

    move_only& operator=(move_only&& other) noexcept {
        if(this != &other) {
            value = other.value;
            other.value = -1;
        }
        return *this;
    }

    bool operator==(const move_only& rhs) const noexcept {
        return value == rhs.value;
    }
};

static int nontrivial_alive = 0;

struct nontrivial {
    int value;

    nontrivial() : value(0) {
        ++nontrivial_alive;
    }

    explicit nontrivial(int v) : value(v) {
        ++nontrivial_alive;
    }

    nontrivial(const nontrivial& o) : value(o.value) {
        ++nontrivial_alive;
    }

    nontrivial(nontrivial&& o) noexcept : value(o.value) {
        o.value = -1;
        ++nontrivial_alive;
    }

    nontrivial& operator=(const nontrivial& o) {
        value = o.value;
        return *this;
    }

    nontrivial& operator=(nontrivial&& o) noexcept {
        value = o.value;
        o.value = -1;
        return *this;
    }

    ~nontrivial() {
        --nontrivial_alive;
    }

    bool operator==(const nontrivial& rhs) const {
        return value == rhs.value;
    }

    auto operator<=>(const nontrivial& rhs) const = default;
};

constexpr bool constexpr_int_operations() {
    small_vector<int, 4> values{1, 2, 3};
    std::array<int, 2> tail = {4, 5};

    values.append(tail);
    values.insert(values.begin() + 1, 9);
    values.erase(values.begin() + 2);
    values.resize_for_overwrite(7);
    values[5] = 11;
    values[6] = 12;

    const auto popped = values.pop_back_val();
    values.shrink_to_fit();

    return values.size() == 6 && values.front() == 1 && values[1] == 9 && values.back() == 11 &&
           popped == 12 && !values.inlined() &&
           (values <=> small_vector<int, 0>{1, 9, 3, 4, 5, 11}) == std::strong_ordering::equal;
}

constexpr bool constexpr_string_operations() {
    small_vector<std::string, 2> values;
    values.emplace_back("alpha");
    values.emplace_back("beta");
    values.resize(4, std::string("tail"));
    values.pop_back();
    values.shrink_to_fit();

    return values.size() == 3 && values.front() == "alpha" && values[1] == "beta" &&
           values.back() == "tail";
}

constexpr bool constexpr_optional_operations() {
    constexpr auto size =
        small_vector<std::optional<int>, 0>{std::optional<int>{1}, std::nullopt}.size();
    static_assert(size == 2);

    small_vector<std::optional<int>, 1> values;
    std::array<std::optional<int>, 3> source = {
        std::optional<int>{1},
        std::nullopt,
        std::optional<int>{3},
    };

    values.assign(source);
    values.erase(values.begin() + 1);
    values.push_back(std::optional<int>{4});

    return values.size() == 3 && values[0].value() == 1 && values[1].value() == 3 &&
           values[2].value() == 4;
}

constexpr bool constexpr_variant_operations() {
    using value_type = std::variant<int, std::string>;

    small_vector<value_type, 1> values;
    values.emplace_back(1);
    values.emplace_back(std::string("two"));
    values.emplace_back(3);
    values.erase(values.begin());
    values.shrink_to_fit();

    return values.size() == 2 && std::get<std::string>(values[0]) == "two" &&
           std::get<int>(values[1]) == 3;
}

TEST_SUITE(small_vector) {

TEST_CASE(constexpr) {
    static_assert(small_vector{1, 2, 3}.size() == 3);
    static_assert(vector<int>{1, 2, 3, 4}.size() == 4);

    static_assert(constexpr_int_operations());
    static_assert(constexpr_string_operations());
    static_assert(constexpr_optional_operations());
    static_assert(constexpr_variant_operations());
}

TEST_CASE(construction_and_copy_move) {
    // Default and count constructors.
    {
        small_vector<int, 4> values;
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.size(), 0U);
        EXPECT_EQ(values.capacity(), 4U);
        EXPECT_EQ(values.inline_capacity(), 4U);
        EXPECT_TRUE(values.inlined());
        EXPECT_TRUE(values.inlinable());
    }
    {
        small_vector<int, 4> values(3);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_TRUE(values.inlined());
        for(std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i], 0);
        }
    }
    {
        small_vector<int, 2> values(5, 42);
        EXPECT_EQ(values.size(), 5U);
        EXPECT_FALSE(values.inlined());
        for(std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i], 42);
        }
    }

    // Range and initializer-list constructors.
    {
        std::array<int, 5> source = {10, 20, 30, 40, 50};
        small_vector<int, 4> values(source);
        EXPECT_EQ(values.size(), 5U);
        EXPECT_FALSE(values.inlined());
        for(std::size_t i = 0; i < source.size(); ++i) {
            EXPECT_EQ(values[i], source[i]);
        }
    }
    {
        std::array<int, 3> source_data = {1, 2, 3};
        auto source = std::views::all(source_data);
        small_vector<int, 4> values(source);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[1], 2);
        EXPECT_EQ(values[2], 3);
    }
    {
        small_vector<int, 4> values = {1, 2, 3, 4, 5};
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[4], 5);
    }

    // Copy constructors across inline, heap, and cross-capacity storage.
    {
        small_vector<int, 4> source = {1, 2, 3};
        small_vector<int, 4> copy(source);
        EXPECT_EQ(copy, source);
        EXPECT_TRUE(copy.inlined());
    }
    {
        small_vector<int, 2> source = {1, 2, 3, 4, 5};
        small_vector<int, 2> copy(source);
        EXPECT_EQ(copy, source);
        EXPECT_FALSE(copy.inlined());
    }
    {
        small_vector<int, 2> source = {1, 2, 3};
        small_vector<int, 8> copy(source);
        EXPECT_EQ(copy.size(), 3U);
        EXPECT_EQ(copy[0], 1);
        EXPECT_EQ(copy[1], 2);
        EXPECT_EQ(copy[2], 3);
        EXPECT_TRUE(copy.inlined());
    }

    // Move constructors should preserve values and steal heap allocations.
    {
        small_vector<int, 4> source = {1, 2, 3};
        small_vector<int, 4> moved(std::move(source));
        EXPECT_EQ(moved.size(), 3U);
        EXPECT_EQ(moved[0], 1);
        EXPECT_TRUE(moved.inlined());
    }
    {
        small_vector<int, 2> source = {1, 2, 3, 4};
        auto* old_data = source.data();
        small_vector<int, 2> moved(std::move(source));
        EXPECT_EQ(moved.size(), 4U);
        EXPECT_EQ(moved.data(), old_data);
        EXPECT_TRUE(source.empty());
        EXPECT_TRUE(source.inlined());
        EXPECT_EQ(source.capacity(), 2U);
    }
    {
        small_vector<int, 4> source;
        source.reserve(16);
        source.push_back(7);
        auto* old_data = source.data();

        small_vector<int, 4> moved(std::move(source));
        EXPECT_EQ(moved.size(), 1U);
        EXPECT_EQ(moved[0], 7);
        EXPECT_EQ(moved.data(), old_data);
        EXPECT_FALSE(moved.inlined());
    }
    {
        small_vector<int, 2> source = {1, 2, 3};
        auto* old_data = source.data();
        small_vector<int, 8> moved(std::move(source));
        EXPECT_EQ(moved.size(), 3U);
        EXPECT_EQ(moved[0], 1);
        EXPECT_EQ(moved[2], 3);
        EXPECT_EQ(moved.data(), old_data);
        EXPECT_FALSE(moved.inlined());
        EXPECT_TRUE(source.empty());
        EXPECT_TRUE(source.inlined());
        EXPECT_EQ(source.capacity(), 2U);
        source.push_back(9);
        EXPECT_TRUE(source.inlined());
        EXPECT_EQ(source[0], 9);
    }

    // Generator constructors should call the generator once per element.
    {
        int counter = 0;
        small_vector<int, 4> values(5, [&counter]() { return counter++; });
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values[0], 0);
        EXPECT_EQ(values[1], 1);
        EXPECT_EQ(values[4], 4);
    }
}

TEST_CASE(assignment_and_append) {
    // Copy assignment should handle self-assignment and mixed storage shapes.
    {
        small_vector<int, 4> values = {1, 2, 3};
        const auto* self = &values;
        values = *self;
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 1);
    }
    {
        small_vector<int, 4> dst = {1, 2};
        small_vector<int, 4> src = {3, 4, 5};
        dst = src;
        EXPECT_EQ(dst, src);
        EXPECT_TRUE(dst.inlined());
    }
    {
        small_vector<int, 2> dst = {1, 2, 3};
        small_vector<int, 2> src = {4};
        dst = src;
        EXPECT_EQ(dst.size(), 1U);
        EXPECT_EQ(dst[0], 4);
    }
    {
        small_vector<int, 2> dst = {1};
        small_vector<int, 2> src = {4, 5, 6, 7};
        dst = src;
        EXPECT_EQ(dst, src);
        EXPECT_FALSE(dst.inlined());
    }
    {
        small_vector<int, 2> dst = {1, 2, 3};
        small_vector<int, 2> src = {4, 5, 6, 7, 8};
        dst = src;
        EXPECT_EQ(dst, src);
    }
    {
        small_vector<int, 2> src = {1, 2, 3};
        small_vector<int, 8> dst;
        dst.assign(src);
        EXPECT_EQ(dst.size(), 3U);
        EXPECT_EQ(dst[2], 3);
        EXPECT_TRUE(dst.inlined());
    }

    // Move assignment should preserve values and steal heap allocations when possible.
    {
        small_vector<int, 4> values = {1, 2, 3};
        auto* self = &values;
        values = std::move(*self);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 1);
    }
    {
        small_vector<int, 4> dst = {1, 2};
        small_vector<int, 4> src = {3, 4, 5};
        dst = std::move(src);
        EXPECT_EQ(dst.size(), 3U);
        EXPECT_EQ(dst[0], 3);
    }
    {
        small_vector<int, 2> dst = {1};
        small_vector<int, 2> src = {3, 4, 5, 6};
        auto* old_data = src.data();
        dst = std::move(src);
        EXPECT_EQ(dst.size(), 4U);
        EXPECT_EQ(dst.data(), old_data);
        EXPECT_TRUE(src.empty());
        EXPECT_TRUE(src.inlined());
        EXPECT_EQ(src.capacity(), 2U);
    }
    {
        small_vector<int, 2> dst = {1, 2, 3};
        small_vector<int, 2> src = {7};
        dst = std::move(src);
        EXPECT_EQ(dst.size(), 1U);
        EXPECT_EQ(dst[0], 7);
    }
    {
        small_vector<int, 2> src = {1, 2, 3};
        small_vector<int, 8> dst = {10};
        auto* old_data = src.data();
        dst.assign(std::move(src));
        EXPECT_EQ(dst.size(), 3U);
        EXPECT_EQ(dst[0], 1);
        EXPECT_EQ(dst.data(), old_data);
        EXPECT_FALSE(dst.inlined());
        EXPECT_TRUE(src.empty());
        EXPECT_TRUE(src.inlined());
        EXPECT_EQ(src.capacity(), 2U);
        src.push_back(7);
        EXPECT_TRUE(src.inlined());
        EXPECT_EQ(src[0], 7);
    }
    {
        small_vector<int, 2> src = {1, 2};
        small_vector<int, 8> dst = {9, 10, 11};
        dst.assign(std::move(src));
        EXPECT_EQ(dst, small_vector<int, 8>{1, 2});
        EXPECT_TRUE(dst.inlined());
        EXPECT_TRUE(src.empty());
        EXPECT_TRUE(src.inlined());
        EXPECT_EQ(src.capacity(), 2U);
    }

    // Assign APIs should cover counts, ranges, initializer lists, and internal references.
    {
        small_vector<int, 4> values = {1, 2};
        values = {10, 20, 30, 40, 50};
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values[0], 10);
        EXPECT_EQ(values[4], 50);
    }
    {
        small_vector<int, 4> values = {1};
        values.assign(5, 99);
        EXPECT_EQ(values.size(), 5U);
        for(std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i], 99);
        }
    }
    {
        small_vector<int, 2> values = {1, 2, 3, 4};
        auto* old_data = values.data();
        const auto old_capacity = values.capacity();
        values.assign(3, 9);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values.capacity(), old_capacity);
        EXPECT_EQ(values.data(), old_data);
        EXPECT_EQ(values, small_vector<int, 2>{9, 9, 9});
    }
    {
        std::array<int, 3> source = {7, 8, 9};
        small_vector<int, 4> values = {1, 2, 3, 4, 5};
        values.assign(source);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 7);
    }
    {
        small_vector<int, 4> values;
        values.assign({100, 200});
        EXPECT_EQ(values.size(), 2U);
        EXPECT_EQ(values[0], 100);
        EXPECT_EQ(values[1], 200);
    }
    {
        small_vector<int, 2> values = {7, 8, 9};
        values.assign(4, values[0]);
        EXPECT_EQ(values, small_vector<int, 2>{7, 7, 7, 7});
    }

    // Append APIs should accept ranges, initializer lists, copy/move sources, and internal refs.
    {
        small_vector<int, 3> values = {1, 2, 3};
        std::array<int, 3> tail = {4, 5, 6};
        values.append(tail);
        EXPECT_EQ(values, small_vector<int, 3>{1, 2, 3, 4, 5, 6});
    }
    {
        small_vector<int, 4> values = {1, 2};
        values.append({3, 4, 5});
        EXPECT_EQ(values, small_vector<int, 4>{1, 2, 3, 4, 5});
    }
    {
        small_vector<int, 2> values = {3, 4};
        values.append(2, values[0]);
        EXPECT_EQ(values, small_vector<int, 2>{3, 4, 3, 3});
    }
    {
        small_vector<int, 4> src = {4, 5};
        small_vector<int, 4> dst = {1, 2, 3};
        dst.append(src);
        EXPECT_EQ(dst, small_vector<int, 4>{1, 2, 3, 4, 5});
        EXPECT_EQ(src.size(), 2U);
    }
    {
        small_vector<int, 4> src = {4, 5};
        small_vector<int, 4> dst = {1, 2, 3};
        dst.append(std::move(src));
        EXPECT_EQ(dst, small_vector<int, 4>{1, 2, 3, 4, 5});
        EXPECT_TRUE(src.empty());
    }
}

TEST_CASE(modifiers) {
    // push_back and emplace_back should preserve values across inline and heap growth.
    {
        small_vector<std::string, 2> values;
        std::string greeting = "hello";
        values.push_back(greeting);
        values.push_back(std::string("world"));
        values.push_back(std::string("!"));

        EXPECT_EQ(values[0], std::string("hello"));
        EXPECT_EQ(greeting, std::string("hello"));
        EXPECT_EQ(values[1], std::string("world"));
        EXPECT_EQ(values.size(), 3U);
        EXPECT_FALSE(values.inlined());
    }
    {
        small_vector<int, 2> values = {1, 2};
        values.push_back(values[0]);
        EXPECT_EQ(values, small_vector<int, 2>{1, 2, 1});
    }
    {
        small_vector<std::string, 2> values;
        values.emplace_back(3, 'x');
        values.emplace_back("test");
        auto& ref = values.emplace_back("more");

        EXPECT_EQ(values[0], std::string("xxx"));
        EXPECT_EQ(values[1], std::string("test"));
        EXPECT_EQ(ref, std::string("more"));
    }

    // pop_back and pop_back_val should update size and return the removed value.
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.pop_back();
        EXPECT_EQ(values.size(), 2U);
        EXPECT_EQ(values.back(), 2);

        const auto removed = values.pop_back_val();
        EXPECT_EQ(removed, 2);
        EXPECT_EQ(values.size(), 1U);

        values.pop_back();
        EXPECT_TRUE(values.empty());
    }

    // Insert and emplace should cover single elements, counts, ranges, and edge positions.
    {
        small_vector<int, 4> values = {1, 3};
        int middle = 2;
        auto it = values.insert(values.begin() + 1, middle);
        EXPECT_EQ(*it, 2);
        EXPECT_EQ(values, small_vector<int, 4>{1, 2, 3});
    }
    {
        small_vector<std::string, 4> values = {std::string("a"), std::string("c")};
        auto it = values.insert(values.begin() + 1, std::string("b"));
        EXPECT_EQ(*it, std::string("b"));
        EXPECT_EQ(values.size(), 3U);
    }
    {
        small_vector<int, 2> values = {1, 5};
        values.insert(values.begin() + 1, 3, 9);
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values[1], 9);
        EXPECT_EQ(values[3], 9);
        EXPECT_EQ(values[4], 5);
    }
    {
        small_vector<int, 2> values = {1, 2};
        values.insert(values.begin() + 1, values[0]);
        values.insert(values.begin() + 2, 2, values[0]);
        EXPECT_EQ(values, small_vector<int, 2>{1, 1, 1, 1, 2});
    }
    {
        small_vector<int, 4> values = {1, 5};
        std::array<int, 3> source = {2, 3, 4};
        values.insert(values.begin() + 1, source);
        EXPECT_EQ(values, small_vector<int, 4>{1, 2, 3, 4, 5});
    }
    {
        small_vector<int, 4> values = {1, 5};
        std::array<int, 3> source_data = {2, 3, 4};
        auto source = std::views::all(source_data);
        values.insert(values.begin() + 1, source);
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values[1], 2);
        EXPECT_EQ(values[4], 5);
    }
    {
        small_vector<int, 4> values = {1, 5};
        values.insert(values.begin() + 1, {2, 3, 4});
        values.insert(values.begin(), 0);
        values.insert(values.end(), 6);
        EXPECT_EQ(values, small_vector<int, 4>{0, 1, 2, 3, 4, 5, 6});
    }
    {
        small_vector<std::string, 8> values = {"a", "b", "c", "d"};
        auto source = std::ranges::subrange(values.begin(), values.begin() + 2);
        values.insert(values.begin() + 1, source);
        EXPECT_EQ(values,
                  small_vector<std::string, 8>{std::string("a"),
                                               std::string("a"),
                                               std::string("b"),
                                               std::string("b"),
                                               std::string("c"),
                                               std::string("d")});
    }
    {
        small_vector<std::string, 8> values = {"alpha", "beta", "gamma"};
        auto source = std::ranges::subrange(values.begin(), values.end());
        values.insert(values.begin(), source);
        EXPECT_EQ(values,
                  small_vector<std::string, 8>{std::string("alpha"),
                                               std::string("beta"),
                                               std::string("gamma"),
                                               std::string("alpha"),
                                               std::string("beta"),
                                               std::string("gamma")});
    }
    {
        small_vector<std::string, 2> values = {"alpha", "beta", "gamma"};
        auto source = std::ranges::subrange(values.begin(), values.end());
        values.insert(values.begin(), source);
        EXPECT_EQ(values,
                  small_vector<std::string, 2>{std::string("alpha"),
                                               std::string("beta"),
                                               std::string("gamma"),
                                               std::string("alpha"),
                                               std::string("beta"),
                                               std::string("gamma")});
    }
    {
        small_vector<std::string, 4> values;
        values.emplace(values.begin(), "first");
        values.emplace(values.end(), "last");
        values.emplace(values.begin() + 1, 3, 'x');
        EXPECT_EQ(values[0], std::string("first"));
        EXPECT_EQ(values[1], std::string("xxx"));
        EXPECT_EQ(values[2], std::string("last"));
    }

    // Erase and clear should maintain ordering and preserve capacity where appropriate.
    {
        small_vector<int, 4> values = {1, 2, 3, 4};
        auto it = values.erase(values.begin() + 1);
        EXPECT_EQ(*it, 3);
        EXPECT_EQ(values, small_vector<int, 4>{1, 3, 4});
    }
    {
        small_vector<int, 4> values = {1, 2, 3, 4, 5};
        auto it = values.erase(values.begin() + 1, values.begin() + 4);
        EXPECT_EQ(*it, 5);
        EXPECT_EQ(values, small_vector<int, 4>{1, 5});
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.erase(values.begin());
        values.erase(values.end() - 1);
        EXPECT_EQ(values, small_vector<int, 4>{2});
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.erase(values.begin(), values.end());
        EXPECT_TRUE(values.empty());
    }
    {
        small_vector<int, 3> values = {1, 2, 3, 4};
        const auto capacity_before_clear = values.capacity();
        values.clear();
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.capacity(), capacity_before_clear);
        values.push_back(9);
        EXPECT_EQ(values[0], 9);
    }
}

TEST_CASE(capacity_and_storage_management) {
    // resize should cover growth, shrink, overwrite growth, and internal references.
    {
        small_vector<int, 4> values = {1, 2};
        values.resize(5);
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[1], 2);
        EXPECT_EQ(values[2], 0);
        EXPECT_EQ(values[4], 0);
    }
    {
        small_vector<int, 4> values = {1};
        values.resize(4, 77);
        EXPECT_EQ(values.size(), 4U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[3], 77);
    }
    {
        small_vector<int, 2> values = {5, 6};
        values.resize(4, values[0]);
        EXPECT_EQ(values, small_vector<int, 2>{5, 6, 5, 5});
    }
    {
        small_vector<std::string, 2> values = {"alpha", "beta"};
        values.resize(4, values[0]);
        EXPECT_EQ(values.size(), 4U);
        EXPECT_EQ(values[0], "alpha");
        EXPECT_EQ(values[1], "beta");
        EXPECT_EQ(values[2], "alpha");
        EXPECT_EQ(values[3], "alpha");
    }
    {
        small_vector<int, 4> values = {1, 2, 3, 4, 5};
        values.resize(2);
        EXPECT_EQ(values.size(), 2U);
        EXPECT_EQ(values[1], 2);
    }
    {
        small_vector<int, 4> values = {1, 2};
        values.resize_for_overwrite(5);
        values[2] = 20;
        values[3] = 30;
        values[4] = 40;
        values.resize_for_overwrite(3);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[1], 2);
        EXPECT_EQ(values[2], 20);
    }

    // reserve and shrink_to_fit should manage heap transitions in both directions.
    {
        small_vector<int, 4> values;
        values.reserve(100);
        EXPECT_GE(values.capacity(), 100U);
        EXPECT_FALSE(values.inlined());
        EXPECT_TRUE(values.empty());
    }
    {
        small_vector<int, 4> values = {1, 2};
        const auto capacity_before = values.capacity();
        values.reserve(2);
        EXPECT_EQ(values.capacity(), capacity_before);
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.reserve(16);
        EXPECT_FALSE(values.inlined());
        values.shrink_to_fit();
        EXPECT_TRUE(values.inlined());
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[2], 3);
    }
    {
        small_vector<int, 4> values;
        values.reserve(16);
        EXPECT_TRUE(values.empty());
        EXPECT_FALSE(values.inlined());
        values.shrink_to_fit();
        EXPECT_TRUE(values.empty());
        EXPECT_TRUE(values.inlined());
        EXPECT_EQ(values.capacity(), 4U);
        values.push_back(42);
        EXPECT_TRUE(values.inlined());
        EXPECT_EQ(values[0], 42);
    }
    {
        small_vector<int, 2> values;
        for(int i = 0; i < 100; ++i) {
            values.push_back(i);
        }
        values.resize(5);
        const auto old_capacity = values.capacity();
        values.shrink_to_fit();
        EXPECT_LE(values.capacity(), old_capacity);
        EXPECT_EQ(values.size(), 5U);
    }

    // swap, size/capacity accessors, and inline-capacity metadata should stay consistent.
    {
        small_vector<int, 4> a = {1, 2};
        small_vector<int, 4> b = {3, 4, 5};
        a.swap(b);
        EXPECT_EQ(a, small_vector<int, 4>{3, 4, 5});
        EXPECT_EQ(b, small_vector<int, 4>{1, 2});
    }
    {
        small_vector<int, 2> a = {1, 2};
        small_vector<int, 2> b = {3, 4, 5, 6};
        a.swap(b);
        EXPECT_EQ(a.size(), 4U);
        EXPECT_EQ(b.size(), 2U);
        EXPECT_EQ(a[0], 3);
        EXPECT_EQ(b[0], 1);
    }
    {
        small_vector<int, 2> a = {1, 2, 3};
        small_vector<int, 2> b = {4, 5, 6, 7};
        a.swap(b);
        EXPECT_EQ(a.size(), 4U);
        EXPECT_EQ(b.size(), 3U);
        EXPECT_EQ(a[0], 4);
        EXPECT_EQ(b[0], 1);
    }
    {
        small_vector<int, 2> a = {1, 2, 3};
        small_vector<int, 6> b = {7, 8};
        a.swap(b);
        EXPECT_EQ(a, small_vector<int, 2>{7, 8});
        EXPECT_EQ(b, small_vector<int, 6>{1, 2, 3});
        EXPECT_FALSE(a.inlined());
        EXPECT_TRUE(b.inlined());
    }
    {
        small_vector<int, 4> values;
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.size(), 0U);
        EXPECT_EQ(values.capacity(), 4U);
        EXPECT_TRUE(values.max_size() > 0U);

        values.push_back(1);
        EXPECT_FALSE(values.empty());
        EXPECT_EQ(values.size(), 1U);
    }
    {
        small_vector<int, 3> values;
        EXPECT_TRUE(values.inlined());
        EXPECT_TRUE(values.inlinable());
        EXPECT_EQ(values.inline_capacity(), 3U);

        values = {1, 2, 3};
        EXPECT_TRUE(values.inlined());
        EXPECT_TRUE(values.inlinable());

        values.push_back(4);
        EXPECT_FALSE(values.inlined());
        EXPECT_FALSE(values.inlinable());

        values.resize(2);
        EXPECT_FALSE(values.inlined());
        EXPECT_TRUE(values.inlinable());

        values.shrink_to_fit();
        EXPECT_TRUE(values.inlined());
    }
    {
        small_vector<int, 0> values;
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.inline_capacity(), 0U);
        EXPECT_TRUE(values.inlined());

        values.push_back(1);
        values.push_back(2);
        EXPECT_EQ(values.size(), 2U);
        EXPECT_FALSE(values.inlined());
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[1], 2);
    }
}

TEST_CASE(accessors_iterators_comparison_and_erased_capacity_api) {
    // Accessors should work in mutable and const contexts.
    {
        small_vector<int, 4> values = {10, 20, 30};
        EXPECT_EQ(values.at(0), 10);
        EXPECT_EQ(values.at(2), 30);
        EXPECT_EQ(values[0], 10);
        EXPECT_EQ(values[2], 30);

        values[1] = 99;
        values.front() = 11;
        values.back() = 33;
        EXPECT_EQ(values[0], 11);
        EXPECT_EQ(values[1], 99);
        EXPECT_EQ(values[2], 33);

        int* data = values.data();
        EXPECT_EQ(data[0], 11);
        EXPECT_EQ(data[2], 33);

        const auto& const_values = values;
        EXPECT_EQ(const_values.at(1), 99);
        EXPECT_EQ(const_values.front(), 11);
        EXPECT_EQ(const_values.back(), 33);
        EXPECT_EQ(const_values.data()[1], 99);
    }

    // Iterators and range-based loops should traverse in all directions.
    {
        small_vector<int, 4> values = {1, 2, 3, 4};
        int sum = 0;
        for(auto it = values.begin(); it != values.end(); ++it) {
            sum += *it;
        }
        EXPECT_EQ(sum, 10);
        EXPECT_EQ(*values.begin(), 1);
        EXPECT_EQ(*(values.end() - 1), 4);
        EXPECT_EQ(*values.cbegin(), 1);
    }
    {
        const small_vector<int, 4> values = {10, 20, 30};
        int sum = 0;
        for(auto it = values.cbegin(); it != values.cend(); ++it) {
            sum += *it;
        }
        EXPECT_EQ(sum, 60);

        auto rit = values.crbegin();
        EXPECT_EQ(*rit, 30);
        ++rit;
        EXPECT_EQ(*rit, 20);
        ++rit;
        EXPECT_EQ(*rit, 10);
        ++rit;
        EXPECT_TRUE(rit == values.crend());
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        small_vector<int, 4> reversed;
        int product = 1;
        for(auto x: values) {
            product *= x;
        }
        for(auto it = values.rbegin(); it != values.rend(); ++it) {
            reversed.push_back(*it);
        }
        EXPECT_EQ(product, 6);
        EXPECT_EQ(reversed, small_vector<int, 4>{3, 2, 1});
    }

    // Comparison operators and CTAD should work across capacities.
    {
        small_vector<int, 4> a = {1, 2, 3};
        small_vector<int, 4> b = {1, 2, 3};
        small_vector<int, 4> c = {1, 2, 4};
        small_vector<int, 4> d = {1, 2};

        EXPECT_TRUE(a == b);
        EXPECT_FALSE(a != b);
        EXPECT_FALSE(a == c);
        EXPECT_TRUE(a != c);
        EXPECT_FALSE(a == d);
        EXPECT_TRUE(a < c);
        EXPECT_TRUE(c > a);
        EXPECT_TRUE(a <= b);
        EXPECT_TRUE(a >= b);
        EXPECT_TRUE(d < a);
    }
    {
        small_vector<int, 2> a = {1, 2, 3};
        small_vector<int, 8> b = {1, 2, 3};
        small_vector<int, 8> c = {1, 2, 4};
        EXPECT_TRUE(a == b);
        EXPECT_TRUE(a < c);
    }
    {
        std::array<int, 4> source = {1, 2, 3, 4};
        small_vector values(source);
        EXPECT_EQ(values.size(), 4U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[3], 4);
        static_assert(std::is_same_v<decltype(values)::value_type, int>);
    }

    // The erased inline-capacity API should work for read-only and mutable callers.
    {
        small_vector<int, 2> a = {1, 2, 3};
        small_vector<int, 8> b = {4, 5};
        EXPECT_EQ(std::accumulate(a.begin(), a.end(), 0), 6);
        EXPECT_EQ(std::accumulate(b.begin(), b.end(), 0), 9);
    }
    {
        small_vector<int, 2> src = {1, 2, 3};
        small_vector<int, 8> dst = {9};
        dst.assign(src);
        EXPECT_EQ(dst, small_vector<int, 8>{1, 2, 3});

        dst.push_back(dst.back());
        EXPECT_EQ(dst, small_vector<int, 8>{1, 2, 3, 3});

        src.push_back(src.back());
        EXPECT_EQ(src, small_vector<int, 2>{1, 2, 3, 3});
    }
}

TEST_CASE(special_value_types_and_lifetime) {
    // Destruction should clean up non-trivial values after inline and heap growth.
    {
        nontrivial_alive = 0;
        small_vector<nontrivial, 2> values;
        values.emplace_back(1);
        values.emplace_back(2);
        values.emplace_back(3);
        EXPECT_EQ(nontrivial_alive, 3);
    }
    EXPECT_EQ(nontrivial_alive, 0);

    // Move-only values should support emplacement, move construction, and move assignment.
    {
        small_vector<move_only, 2> values;
        values.emplace_back(1);
        values.emplace_back(2);
        values.push_back(move_only{3});
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0].value, 1);
        EXPECT_EQ(values[1].value, 2);
        EXPECT_EQ(values[2].value, 3);
    }
    {
        small_vector<move_only, 2> src;
        src.emplace_back(10);
        src.emplace_back(20);
        small_vector<move_only, 2> moved(std::move(src));
        EXPECT_EQ(moved.size(), 2U);
        EXPECT_EQ(moved[0].value, 10);
        EXPECT_EQ(moved[1].value, 20);
    }
    {
        small_vector<move_only, 2> src;
        src.emplace_back(5);
        src.emplace_back(6);

        small_vector<move_only, 2> dst;
        dst.emplace_back(99);
        dst = std::move(src);

        EXPECT_EQ(dst.size(), 2U);
        EXPECT_EQ(dst[0].value, 5);
        EXPECT_EQ(dst[1].value, 6);
    }

    // Trivially copyable and non-trivial types should both survive growth and reassignment.
    {
        static_assert(std::is_trivially_copyable_v<int>);
        small_vector<int, 4> values = {1, 2, 3, 4};
        small_vector<int, 4> copy = values;
        small_vector<int, 4> moved = std::move(copy);

        EXPECT_EQ(values, moved);
        moved.insert(moved.begin() + 2, 99);
        EXPECT_EQ(moved[2], 99);
        EXPECT_EQ(moved[3], 3);
    }
    {
        nontrivial_alive = 0;
        small_vector<nontrivial, 2> values;
        values.emplace_back(1);
        values.emplace_back(2);
        EXPECT_EQ(nontrivial_alive, 2);

        values.emplace_back(3);
        EXPECT_EQ(nontrivial_alive, 3);
        EXPECT_EQ(values[0].value, 1);
        EXPECT_EQ(values[2].value, 3);

        values.erase(values.begin());
        EXPECT_EQ(nontrivial_alive, 2);

        values.clear();
        EXPECT_EQ(nontrivial_alive, 0);
    }
    {
        nontrivial_alive = 0;
        small_vector<nontrivial, 2> src;
        src.emplace_back(10);
        src.emplace_back(20);

        small_vector<nontrivial, 2> copy(src);
        EXPECT_EQ(nontrivial_alive, 4);
        EXPECT_EQ(copy[0].value, 10);

        small_vector<nontrivial, 2> assigned;
        assigned = src;
        EXPECT_EQ(nontrivial_alive, 6);
    }
    EXPECT_EQ(nontrivial_alive, 0);
}

TEST_CASE(growth_and_edge_cases) {
    // Repeated growth should preserve values and support post-construction algorithms.
    {
        small_vector<int, 4> values;
        constexpr int count = 1000;
        for(int i = 0; i < count; ++i) {
            values.push_back(i);
        }

        EXPECT_EQ(values.size(), static_cast<std::size_t>(count));
        EXPECT_FALSE(values.inlined());
        for(int i = 0; i < count; ++i) {
            EXPECT_EQ(values[static_cast<std::size_t>(i)], i);
        }
    }
    {
        small_vector<int, 2> values(500);
        std::iota(values.begin(), values.end(), 0);
        EXPECT_EQ(values.size(), 500U);
        EXPECT_EQ(values[0], 0);
        EXPECT_EQ(values[499], 499);
    }

    // Empty containers should support erase, insert, swap, and reserve transitions.
    {
        small_vector<int, 4> values;
        const auto it = values.erase(values.begin(), values.end());
        EXPECT_TRUE(it == values.end());
        EXPECT_TRUE(values.empty());
    }
    {
        small_vector<int, 4> values;
        values.insert(values.begin(), 42);
        EXPECT_EQ(values.size(), 1U);
        EXPECT_EQ(values[0], 42);
    }
    {
        small_vector<int, 4> values;
        std::array<int, 3> source = {1, 2, 3};
        values.insert(values.begin(), source);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 1);
    }
    {
        small_vector<int, 4> values;
        values.insert(values.begin(), 3, 7);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 7);
        EXPECT_EQ(values[2], 7);
    }
    {
        small_vector<int, 4> a;
        small_vector<int, 4> b = {1, 2, 3};
        a.swap(b);
        EXPECT_EQ(a.size(), 3U);
        EXPECT_TRUE(b.empty());
    }
    {
        small_vector<int, 4> values;
        values.reserve(10);
        EXPECT_TRUE(values.empty());
        EXPECT_GE(values.capacity(), 10U);
        values.push_back(1);
        EXPECT_EQ(values.size(), 1U);
    }
}

TEST_CASE(truncate_and_pop_back_n) {
    // truncate should reduce size without changing capacity.
    {
        small_vector<int, 4> values = {1, 2, 3, 4, 5};
        values.truncate(3);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[2], 3);
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.truncate(3);
        EXPECT_EQ(values.size(), 3U);
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.truncate(0);
        EXPECT_TRUE(values.empty());
    }
    {
        nontrivial_alive = 0;
        {
            small_vector<nontrivial, 2> values;
            values.emplace_back(1);
            values.emplace_back(2);
            values.emplace_back(3);
            EXPECT_EQ(nontrivial_alive, 3);
            values.truncate(1);
            EXPECT_EQ(nontrivial_alive, 1);
            EXPECT_EQ(values[0].value, 1);
        }
        EXPECT_EQ(nontrivial_alive, 0);
    }

    // pop_back_n should remove the last N elements.
    {
        small_vector<int, 4> values = {1, 2, 3, 4, 5};
        values.pop_back_n(2);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values.back(), 3);
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.pop_back_n(3);
        EXPECT_TRUE(values.empty());
    }
    {
        small_vector<int, 4> values = {1, 2, 3};
        values.pop_back_n(0);
        EXPECT_EQ(values.size(), 3U);
    }
    {
        nontrivial_alive = 0;
        {
            small_vector<nontrivial, 2> values;
            values.emplace_back(1);
            values.emplace_back(2);
            values.emplace_back(3);
            values.emplace_back(4);
            EXPECT_EQ(nontrivial_alive, 4);
            values.pop_back_n(2);
            EXPECT_EQ(nontrivial_alive, 2);
            EXPECT_EQ(values[0].value, 1);
            EXPECT_EQ(values[1].value, 2);
        }
        EXPECT_EQ(nontrivial_alive, 0);
    }
}

TEST_CASE(byte_size_accessors) {
    {
        small_vector<int, 4> values = {1, 2, 3};
        EXPECT_EQ(values.size_in_bytes(), 3 * sizeof(int));
        EXPECT_EQ(values.capacity_in_bytes(), 4 * sizeof(int));
    }
    {
        small_vector<int, 4> values;
        EXPECT_EQ(values.size_in_bytes(), 0U);
    }
    {
        small_vector<double, 2> values = {1.0, 2.0, 3.0};
        EXPECT_EQ(values.size_in_bytes(), 3 * sizeof(double));
        EXPECT_GE(values.capacity_in_bytes(), 3 * sizeof(double));
    }
}

TEST_CASE(nontrivial_swap) {
    // Swap with non-trivial types should not leak or double-destroy.
    {
        nontrivial_alive = 0;
        {
            small_vector<nontrivial, 2> a;
            a.emplace_back(1);
            a.emplace_back(2);
            small_vector<nontrivial, 2> b;
            b.emplace_back(3);
            b.emplace_back(4);
            b.emplace_back(5);
            EXPECT_EQ(nontrivial_alive, 5);

            a.swap(b);
            EXPECT_EQ(nontrivial_alive, 5);
            EXPECT_EQ(a.size(), 3U);
            EXPECT_EQ(b.size(), 2U);
            EXPECT_EQ(a[0].value, 3);
            EXPECT_EQ(a[1].value, 4);
            EXPECT_EQ(a[2].value, 5);
            EXPECT_EQ(b[0].value, 1);
            EXPECT_EQ(b[1].value, 2);
        }
        EXPECT_EQ(nontrivial_alive, 0);
    }
    // Both on heap.
    {
        nontrivial_alive = 0;
        {
            small_vector<nontrivial, 1> a;
            a.emplace_back(1);
            a.emplace_back(2);
            small_vector<nontrivial, 1> b;
            b.emplace_back(3);
            b.emplace_back(4);
            b.emplace_back(5);
            EXPECT_EQ(nontrivial_alive, 5);

            a.swap(b);
            EXPECT_EQ(nontrivial_alive, 5);
            EXPECT_EQ(a.size(), 3U);
            EXPECT_EQ(b.size(), 2U);
            EXPECT_EQ(a[0].value, 3);
            EXPECT_EQ(b[0].value, 1);
        }
        EXPECT_EQ(nontrivial_alive, 0);
    }
    // One empty.
    {
        nontrivial_alive = 0;
        {
            small_vector<nontrivial, 2> a;
            small_vector<nontrivial, 2> b;
            b.emplace_back(1);
            b.emplace_back(2);
            EXPECT_EQ(nontrivial_alive, 2);

            a.swap(b);
            EXPECT_EQ(nontrivial_alive, 2);
            EXPECT_EQ(a.size(), 2U);
            EXPECT_TRUE(b.empty());
            EXPECT_EQ(a[0].value, 1);
            EXPECT_EQ(a[1].value, 2);
        }
        EXPECT_EQ(nontrivial_alive, 0);
    }
}

TEST_CASE(vector_alias) {
    // vector<T> is small_vector<T, 0> — no inline storage, always heap.
    {
        vector<int> values;
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(values.inline_capacity(), 0U);
        EXPECT_TRUE(values.inlined());

        values.push_back(1);
        values.push_back(2);
        values.push_back(3);
        EXPECT_EQ(values.size(), 3U);
        EXPECT_FALSE(values.inlined());
        EXPECT_EQ(values[0], 1);
        EXPECT_EQ(values[2], 3);
    }
    {
        vector<int> values = {1, 2, 3, 4, 5};
        EXPECT_EQ(values.size(), 5U);
        EXPECT_FALSE(values.inlined());

        vector<int> copy(values);
        EXPECT_EQ(copy, values);

        vector<int> moved(std::move(copy));
        EXPECT_EQ(moved, values);
        EXPECT_TRUE(copy.empty());
    }
    {
        vector<std::string> values;
        values.emplace_back("hello");
        values.emplace_back("world");
        values.insert(values.begin() + 1, std::string("beautiful"));
        EXPECT_EQ(values.size(), 3U);
        EXPECT_EQ(values[0], "hello");
        EXPECT_EQ(values[1], "beautiful");
        EXPECT_EQ(values[2], "world");

        values.erase(values.begin());
        EXPECT_EQ(values.size(), 2U);
        EXPECT_EQ(values[0], "beautiful");
    }
    {
        vector<int> values = {5, 4, 3, 2, 1};
        values.shrink_to_fit();
        EXPECT_EQ(values.size(), 5U);
        EXPECT_EQ(values.capacity(), 5U);
        EXPECT_EQ(values[0], 5);
        EXPECT_EQ(values[4], 1);
    }
    {
        nontrivial_alive = 0;
        {
            vector<nontrivial> values;
            values.emplace_back(1);
            values.emplace_back(2);
            values.emplace_back(3);
            EXPECT_EQ(nontrivial_alive, 3);
            values.pop_back_n(2);
            EXPECT_EQ(nontrivial_alive, 1);
        }
        EXPECT_EQ(nontrivial_alive, 0);
    }
}

TEST_CASE(assign_from_empty_hybrid_vector) {
    // Assigning from an empty hybrid_vector&& should preserve inline capacity.
    {
        small_vector<int, 4> dst = {1, 2, 3};
        small_vector<int, 2> src;
        hybrid_vector<int>& ref = src;
        dst.assign(std::move(ref));
        EXPECT_TRUE(dst.empty());
        EXPECT_EQ(dst.capacity(), 4U);
        EXPECT_TRUE(dst.inlined());

        dst.push_back(42);
        EXPECT_TRUE(dst.inlined());
        EXPECT_EQ(dst[0], 42);
    }
    // Assigning from a non-empty inline hybrid_vector&& should also work.
    {
        small_vector<int, 4> dst = {1, 2, 3};
        small_vector<int, 2> src = {7, 8};
        hybrid_vector<int>& ref = src;
        dst.assign(std::move(ref));
        EXPECT_EQ(dst.size(), 2U);
        EXPECT_EQ(dst[0], 7);
        EXPECT_EQ(dst[1], 8);
        EXPECT_TRUE(dst.inlined());
    }
    // Assigning from a heap-allocated hybrid_vector&& should steal.
    {
        small_vector<int, 2> dst = {1};
        small_vector<int, 2> src = {3, 4, 5, 6};
        auto* old_data = src.data();
        hybrid_vector<int>& ref = src;
        dst.assign(std::move(ref));
        EXPECT_EQ(dst.size(), 4U);
        EXPECT_EQ(dst.data(), old_data);
        EXPECT_EQ(dst[0], 3);
    }
}

#ifdef __cpp_exceptions

struct throwing_copy {
    inline static int throw_after = -1;
    int value = 0;

    throwing_copy() = default;

    explicit throwing_copy(int v) : value(v) {}

    throwing_copy(const throwing_copy& other) : value(other.value) {
        if(throw_after == 0) {
            throw std::runtime_error("copy failed");
        }
        if(throw_after > 0) {
            --throw_after;
        }
    }

    throwing_copy(throwing_copy&& other) noexcept : value(other.value) {
        other.value = -1;
    }

    throwing_copy& operator=(const throwing_copy& other) = default;
    throwing_copy& operator=(throwing_copy&& other) noexcept = default;

    bool operator==(const throwing_copy& rhs) const noexcept {
        return value == rhs.value;
    }
};

TEST_CASE(exception_safety) {
    // Throwing accessors and modifiers should preserve existing state.
    {
        small_vector<int, 2> values = {1};
        EXPECT_EQ(values.at(0), 1);
        EXPECT_THROWS(values.at(1));
        EXPECT_THROWS(values.at(100));
    }
    {
        small_vector<throwing_copy, 2> values;
        values.push_back(throwing_copy{1});
        values.push_back(throwing_copy{2});

        const int before0 = values[0].value;
        const int before1 = values[1].value;

        throwing_copy::throw_after = 0;
        EXPECT_THROWS(values.push_back(values[0]));
        throwing_copy::throw_after = -1;

        EXPECT_EQ(values.size(), 2U);
        EXPECT_EQ(values[0].value, before0);
        EXPECT_EQ(values[1].value, before1);
    }
    {
        small_vector<throwing_copy, 4> values;
        values.push_back(throwing_copy{1});
        values.push_back(throwing_copy{2});
        values.push_back(throwing_copy{3});

        const auto size_before = values.size();
        throwing_copy candidate{99};
        throwing_copy::throw_after = 0;
        EXPECT_THROWS(values.insert(values.begin() + 1, candidate));
        throwing_copy::throw_after = -1;

        EXPECT_LE(values.size(), size_before + 1);
    }
}

#endif  // __cpp_exceptions

};  // TEST_SUITE(small_vector)

}  // namespace

}  // namespace eventide
