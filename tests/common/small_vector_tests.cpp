#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>
#include <sstream>
#include <string>

#ifdef __cpp_exceptions
#include <stdexcept>
#endif

#include "eventide/zest/zest.h"
#include "eventide/common/small_vector.h"

namespace eventide {

namespace {

// Helper: move-only type
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

// Helper: non-trivial type that tracks construction/destruction
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

// Helper: minimal input iterator wrapping a pointer
template <typename T>
struct input_iter {
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    const T* ptr;

    input_iter() : ptr(nullptr) {}

    explicit input_iter(const T* p) : ptr(p) {}

    reference operator*() const {
        return *ptr;
    }

    pointer operator->() const {
        return ptr;
    }

    input_iter& operator++() {
        ++ptr;
        return *this;
    }

    input_iter operator++(int) {
        auto tmp = *this;
        ++ptr;
        return tmp;
    }

    bool operator==(const input_iter& o) const {
        return ptr == o.ptr;
    }
};

TEST_SUITE(common_small_vector) {

// ---------------------------------------------------------------------------
// 1. Default constructor
// ---------------------------------------------------------------------------
TEST_CASE(default_constructor) {
    small_vector<int, 4> v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
    EXPECT_EQ(v.capacity(), 4U);
    EXPECT_EQ(v.inline_capacity(), 4U);
    EXPECT_TRUE(v.inlined());
    EXPECT_TRUE(v.inlinable());
}

// ---------------------------------------------------------------------------
// 2. Count constructors
// ---------------------------------------------------------------------------
TEST_CASE(count_constructor_default) {
    small_vector<int, 4> v(3);
    EXPECT_EQ(v.size(), 3U);
    for(std::size_t i = 0; i < v.size(); ++i)
        EXPECT_EQ(v[i], 0);
    EXPECT_TRUE(v.inlined());
}

TEST_CASE(count_constructor_value) {
    small_vector<int, 2> v(5, 42);
    EXPECT_EQ(v.size(), 5U);
    EXPECT_FALSE(v.inlined());
    for(std::size_t i = 0; i < v.size(); ++i)
        EXPECT_EQ(v[i], 42);
}

// ---------------------------------------------------------------------------
// 3. Range constructors
// ---------------------------------------------------------------------------
TEST_CASE(range_constructor_forward_iterator) {
    std::array<int, 5> arr = {10, 20, 30, 40, 50};
    small_vector<int, 4> v(arr.begin(), arr.end());
    EXPECT_EQ(v.size(), 5U);
    EXPECT_FALSE(v.inlined());
    for(std::size_t i = 0; i < arr.size(); ++i)
        EXPECT_EQ(v[i], arr[i]);
}

TEST_CASE(range_constructor_input_iterator) {
    int data[] = {1, 2, 3};
    small_vector<int, 4> v(input_iter<int>(data), input_iter<int>(data + 3));
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

// ---------------------------------------------------------------------------
// 4. Initializer list constructor
// ---------------------------------------------------------------------------
TEST_CASE(initializer_list_constructor) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    EXPECT_EQ(v.size(), 5U);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[4], 5);
}

// ---------------------------------------------------------------------------
// 5. Copy constructor
// ---------------------------------------------------------------------------
TEST_CASE(copy_constructor_inline) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b(a);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(b.inlined());
}

TEST_CASE(copy_constructor_allocated) {
    small_vector<int, 2> a = {1, 2, 3, 4, 5};
    small_vector<int, 2> b(a);
    EXPECT_EQ(a, b);
    EXPECT_FALSE(b.inlined());
}

TEST_CASE(copy_constructor_cross_capacity) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 8> b(a);
    EXPECT_EQ(b.size(), 3U);
    EXPECT_EQ(b[0], 1);
    EXPECT_EQ(b[1], 2);
    EXPECT_EQ(b[2], 3);
    EXPECT_TRUE(b.inlined());
}

// ---------------------------------------------------------------------------
// 6. Move constructor
// ---------------------------------------------------------------------------
TEST_CASE(move_constructor_inline) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b(std::move(a));
    EXPECT_EQ(b.size(), 3U);
    EXPECT_EQ(b[0], 1);
    EXPECT_TRUE(b.inlined());
}

TEST_CASE(move_constructor_allocated) {
    small_vector<int, 2> a = {1, 2, 3, 4};
    auto* old_data = a.data();
    small_vector<int, 2> b(std::move(a));
    EXPECT_EQ(b.size(), 4U);
    EXPECT_EQ(b.data(), old_data);  // should steal the allocation
    EXPECT_TRUE(a.empty());
}

TEST_CASE(move_constructor_cross_capacity) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 8> b(std::move(a));
    EXPECT_EQ(b.size(), 3U);
    EXPECT_EQ(b[0], 1);
    EXPECT_EQ(b[2], 3);
    EXPECT_TRUE(b.inlined());
}

// ---------------------------------------------------------------------------
// 7. Generator constructor
// ---------------------------------------------------------------------------
TEST_CASE(generator_constructor) {
    int counter = 0;
    small_vector<int, 4> v(5, [&counter]() { return counter++; });
    EXPECT_EQ(v.size(), 5U);
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[1], 1);
    EXPECT_EQ(v[4], 4);
}

// ---------------------------------------------------------------------------
// 8. Destructor - proper cleanup of non-trivial types
// ---------------------------------------------------------------------------
TEST_CASE(destructor_cleanup) {
    nontrivial_alive = 0;
    {
        small_vector<nontrivial, 2> v;
        v.emplace_back(1);
        v.emplace_back(2);
        v.emplace_back(3);
        EXPECT_EQ(nontrivial_alive, 3);
    }
    EXPECT_EQ(nontrivial_alive, 0);
}

// ---------------------------------------------------------------------------
// 9. Copy assignment
// ---------------------------------------------------------------------------
TEST_CASE(copy_assignment_self) {
    small_vector<int, 4> v = {1, 2, 3};
    const auto* p = &v;
    v = *p;
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 1);
}

TEST_CASE(copy_assignment_inline_to_inline) {
    small_vector<int, 4> a = {1, 2};
    small_vector<int, 4> b = {3, 4, 5};
    a = b;
    EXPECT_EQ(a, b);
    EXPECT_TRUE(a.inlined());
}

TEST_CASE(copy_assignment_inline_to_allocated) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 2> b = {4};
    a = b;
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0], 4);
}

TEST_CASE(copy_assignment_allocated_to_inline) {
    small_vector<int, 2> a = {1};
    small_vector<int, 2> b = {4, 5, 6, 7};
    a = b;
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a.inlined());
}

TEST_CASE(copy_assignment_allocated_to_allocated) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 2> b = {4, 5, 6, 7, 8};
    a = b;
    EXPECT_EQ(a, b);
}

TEST_CASE(copy_assignment_cross_capacity) {
    small_vector<int, 2> src = {1, 2, 3};
    small_vector<int, 8> dst;
    dst.assign(src);
    EXPECT_EQ(dst.size(), 3U);
    EXPECT_EQ(dst[2], 3);
    EXPECT_TRUE(dst.inlined());
}

// ---------------------------------------------------------------------------
// 10. Move assignment
// ---------------------------------------------------------------------------
TEST_CASE(move_assignment_self) {
    small_vector<int, 4> v = {1, 2, 3};
    auto* p = &v;
    v = std::move(*p);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 1);
}

TEST_CASE(move_assignment_inline_to_inline) {
    small_vector<int, 4> a = {1, 2};
    small_vector<int, 4> b = {3, 4, 5};
    a = std::move(b);
    EXPECT_EQ(a.size(), 3U);
    EXPECT_EQ(a[0], 3);
}

TEST_CASE(move_assignment_allocated_steal) {
    small_vector<int, 2> a = {1};
    small_vector<int, 2> b = {3, 4, 5, 6};
    auto* old_data = b.data();
    a = std::move(b);
    EXPECT_EQ(a.size(), 4U);
    EXPECT_EQ(a.data(), old_data);
}

TEST_CASE(move_assignment_allocated_to_inline) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 2> b = {7};
    a = std::move(b);
    EXPECT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0], 7);
}

TEST_CASE(move_assignment_cross_capacity) {
    small_vector<int, 2> src = {1, 2, 3};
    small_vector<int, 8> dst = {10};
    dst.assign(std::move(src));
    EXPECT_EQ(dst.size(), 3U);
    EXPECT_EQ(dst[0], 1);
    EXPECT_TRUE(dst.inlined());
}

// ---------------------------------------------------------------------------
// 11. Initializer list assignment
// ---------------------------------------------------------------------------
TEST_CASE(initializer_list_assignment) {
    small_vector<int, 4> v = {1, 2};
    v = {10, 20, 30, 40, 50};
    EXPECT_EQ(v.size(), 5U);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[4], 50);
}

// ---------------------------------------------------------------------------
// 12. assign()
// ---------------------------------------------------------------------------
TEST_CASE(assign_count_value) {
    small_vector<int, 4> v = {1};
    v.assign(5, 99);
    EXPECT_EQ(v.size(), 5U);
    for(std::size_t i = 0; i < v.size(); ++i)
        EXPECT_EQ(v[i], 99);
}

TEST_CASE(assign_range) {
    std::array<int, 3> arr = {7, 8, 9};
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    v.assign(arr.begin(), arr.end());
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 7);
}

TEST_CASE(assign_initializer_list) {
    small_vector<int, 4> v;
    v.assign({100, 200});
    EXPECT_EQ(v.size(), 2U);
    EXPECT_EQ(v[0], 100);
    EXPECT_EQ(v[1], 200);
}

// ---------------------------------------------------------------------------
// 13. push_back
// ---------------------------------------------------------------------------
TEST_CASE(push_back_copy_and_move) {
    small_vector<std::string, 2> v;
    std::string s = "hello";
    v.push_back(s);
    EXPECT_EQ(v[0], std::string("hello"));
    EXPECT_EQ(s, std::string("hello"));  // original preserved

    v.push_back(std::string("world"));
    EXPECT_EQ(v[1], std::string("world"));
    EXPECT_TRUE(v.inlined());

    v.push_back(std::string("!"));
    EXPECT_EQ(v.size(), 3U);
    EXPECT_FALSE(v.inlined());
}

// ---------------------------------------------------------------------------
// 14. emplace_back
// ---------------------------------------------------------------------------
TEST_CASE(emplace_back_various) {
    small_vector<std::string, 2> v;
    v.emplace_back(3, 'x');
    EXPECT_EQ(v[0], std::string("xxx"));

    v.emplace_back("test");
    EXPECT_EQ(v[1], std::string("test"));

    auto& ref = v.emplace_back("more");
    EXPECT_EQ(ref, std::string("more"));
}

// ---------------------------------------------------------------------------
// 15. pop_back
// ---------------------------------------------------------------------------
TEST_CASE(pop_back) {
    small_vector<int, 4> v = {1, 2, 3};
    v.pop_back();
    EXPECT_EQ(v.size(), 2U);
    EXPECT_EQ(v.back(), 2);

    v.pop_back();
    v.pop_back();
    EXPECT_TRUE(v.empty());
}

// ---------------------------------------------------------------------------
// 16. insert
// ---------------------------------------------------------------------------
TEST_CASE(insert_single_copy) {
    small_vector<int, 4> v = {1, 3};
    int val = 2;
    auto it = v.insert(v.begin() + 1, val);
    EXPECT_EQ(*it, 2);
    EXPECT_EQ(v, (small_vector<int, 4>{1, 2, 3}));
}

TEST_CASE(insert_single_move) {
    small_vector<std::string, 4> v = {std::string("a"), std::string("c")};
    auto it = v.insert(v.begin() + 1, std::string("b"));
    EXPECT_EQ(*it, std::string("b"));
    EXPECT_EQ(v.size(), 3U);
}

TEST_CASE(insert_count_value) {
    small_vector<int, 2> v = {1, 5};
    v.insert(v.begin() + 1, 3, 9);
    EXPECT_EQ(v.size(), 5U);
    EXPECT_EQ(v[1], 9);
    EXPECT_EQ(v[3], 9);
    EXPECT_EQ(v[4], 5);
}

TEST_CASE(insert_range_forward) {
    small_vector<int, 4> v = {1, 5};
    std::array<int, 3> arr = {2, 3, 4};
    v.insert(v.begin() + 1, arr.begin(), arr.end());
    EXPECT_EQ(v, (small_vector<int, 4>{1, 2, 3, 4, 5}));
}

TEST_CASE(insert_range_input) {
    small_vector<int, 4> v = {1, 5};
    int data[] = {2, 3, 4};
    v.insert(v.begin() + 1, input_iter<int>(data), input_iter<int>(data + 3));
    EXPECT_EQ(v.size(), 5U);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[4], 5);
}

TEST_CASE(insert_initializer_list) {
    small_vector<int, 4> v = {1, 5};
    v.insert(v.begin() + 1, {2, 3, 4});
    EXPECT_EQ(v, (small_vector<int, 4>{1, 2, 3, 4, 5}));
}

TEST_CASE(insert_at_beginning) {
    small_vector<int, 4> v = {2, 3};
    v.insert(v.begin(), 1);
    EXPECT_EQ(v[0], 1);
}

TEST_CASE(insert_at_end) {
    small_vector<int, 4> v = {1, 2};
    v.insert(v.end(), 3);
    EXPECT_EQ(v.back(), 3);
}

// ---------------------------------------------------------------------------
// 17. emplace
// ---------------------------------------------------------------------------
TEST_CASE(emplace_at_positions) {
    small_vector<std::string, 4> v;
    v.emplace(v.begin(), "first");
    v.emplace(v.end(), "last");
    v.emplace(v.begin() + 1, 3, 'x');
    EXPECT_EQ(v[0], std::string("first"));
    EXPECT_EQ(v[1], std::string("xxx"));
    EXPECT_EQ(v[2], std::string("last"));
}

// ---------------------------------------------------------------------------
// 18. erase
// ---------------------------------------------------------------------------
TEST_CASE(erase_single) {
    small_vector<int, 4> v = {1, 2, 3, 4};
    auto it = v.erase(v.begin() + 1);
    EXPECT_EQ(*it, 3);
    EXPECT_EQ(v, (small_vector<int, 4>{1, 3, 4}));
}

TEST_CASE(erase_range) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 1, v.begin() + 4);
    EXPECT_EQ(*it, 5);
    EXPECT_EQ(v, (small_vector<int, 4>{1, 5}));
}

TEST_CASE(erase_first) {
    small_vector<int, 4> v = {1, 2, 3};
    v.erase(v.begin());
    EXPECT_EQ(v, (small_vector<int, 4>{2, 3}));
}

TEST_CASE(erase_last) {
    small_vector<int, 4> v = {1, 2, 3};
    v.erase(v.end() - 1);
    EXPECT_EQ(v, (small_vector<int, 4>{1, 2}));
}

TEST_CASE(erase_all_range) {
    small_vector<int, 4> v = {1, 2, 3};
    v.erase(v.begin(), v.end());
    EXPECT_TRUE(v.empty());
}

// ---------------------------------------------------------------------------
// 19. clear
// ---------------------------------------------------------------------------
TEST_CASE(clear) {
    small_vector<int, 3> v = {1, 2, 3, 4};
    auto cap = v.capacity();
    v.clear();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.capacity(), cap);  // capacity preserved

    v.push_back(9);
    EXPECT_EQ(v[0], 9);
}

// ---------------------------------------------------------------------------
// 20. resize
// ---------------------------------------------------------------------------
TEST_CASE(resize_grow_default) {
    small_vector<int, 4> v = {1, 2};
    v.resize(5);
    EXPECT_EQ(v.size(), 5U);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 0);
    EXPECT_EQ(v[4], 0);
}

TEST_CASE(resize_grow_value) {
    small_vector<int, 4> v = {1};
    v.resize(4, 77);
    EXPECT_EQ(v.size(), 4U);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[3], 77);
}

TEST_CASE(resize_shrink) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5};
    v.resize(2);
    EXPECT_EQ(v.size(), 2U);
    EXPECT_EQ(v[1], 2);
}

// ---------------------------------------------------------------------------
// 21. reserve
// ---------------------------------------------------------------------------
TEST_CASE(reserve_grow) {
    small_vector<int, 4> v;
    v.reserve(100);
    EXPECT_GE(v.capacity(), 100U);
    EXPECT_FALSE(v.inlined());
    EXPECT_TRUE(v.empty());
}

TEST_CASE(reserve_noop) {
    small_vector<int, 4> v = {1, 2};
    auto cap = v.capacity();
    v.reserve(2);
    EXPECT_EQ(v.capacity(), cap);
}

// ---------------------------------------------------------------------------
// 22. shrink_to_fit
// ---------------------------------------------------------------------------
TEST_CASE(shrink_to_fit_back_to_inline) {
    small_vector<int, 4> v = {1, 2, 3};
    v.reserve(16);
    EXPECT_FALSE(v.inlined());

    v.shrink_to_fit();
    EXPECT_TRUE(v.inlined());
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[2], 3);
}

TEST_CASE(shrink_to_fit_reduce_heap) {
    small_vector<int, 2> v;
    for(int i = 0; i < 100; ++i)
        v.push_back(i);
    v.resize(5);
    auto old_cap = v.capacity();
    v.shrink_to_fit();
    EXPECT_LE(v.capacity(), old_cap);
    EXPECT_EQ(v.size(), 5U);
}

// ---------------------------------------------------------------------------
// 23. swap
// ---------------------------------------------------------------------------
TEST_CASE(swap_inline_inline) {
    small_vector<int, 4> a = {1, 2};
    small_vector<int, 4> b = {3, 4, 5};
    a.swap(b);
    EXPECT_EQ(a, (small_vector<int, 4>{3, 4, 5}));
    EXPECT_EQ(b, (small_vector<int, 4>{1, 2}));
}

TEST_CASE(swap_inline_heap) {
    small_vector<int, 2> a = {1, 2};
    small_vector<int, 2> b = {3, 4, 5, 6};
    EXPECT_TRUE(a.inlined());
    EXPECT_FALSE(b.inlined());

    a.swap(b);
    EXPECT_EQ(a.size(), 4U);
    EXPECT_EQ(b.size(), 2U);
    EXPECT_EQ(a[0], 3);
    EXPECT_EQ(b[0], 1);
}

TEST_CASE(swap_heap_heap) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 2> b = {4, 5, 6, 7};
    a.swap(b);
    EXPECT_EQ(a.size(), 4U);
    EXPECT_EQ(b.size(), 3U);
    EXPECT_EQ(a[0], 4);
    EXPECT_EQ(b[0], 1);
}

TEST_CASE(swap_cross_capacity) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 6> b = {7, 8};
    a.swap(b);
    EXPECT_EQ(a, (small_vector<int, 2>{7, 8}));
    EXPECT_EQ(b, (small_vector<int, 6>{1, 2, 3}));
    EXPECT_TRUE(a.inlined());
}

// ---------------------------------------------------------------------------
// 24. at
// ---------------------------------------------------------------------------
TEST_CASE(at_valid) {
    small_vector<int, 4> v = {10, 20, 30};
    EXPECT_EQ(v.at(0), 10);
    EXPECT_EQ(v.at(2), 30);

    const auto& cv = v;
    EXPECT_EQ(cv.at(1), 20);
}

// ---------------------------------------------------------------------------
// 25. operator[]
// ---------------------------------------------------------------------------
TEST_CASE(subscript_operator) {
    small_vector<int, 4> v = {5, 10, 15};
    EXPECT_EQ(v[0], 5);
    EXPECT_EQ(v[2], 15);

    v[1] = 99;
    EXPECT_EQ(v[1], 99);
}

// ---------------------------------------------------------------------------
// 26. front/back
// ---------------------------------------------------------------------------
TEST_CASE(front_back) {
    small_vector<int, 4> v = {1, 2, 3};
    EXPECT_EQ(v.front(), 1);
    EXPECT_EQ(v.back(), 3);

    v.front() = 10;
    v.back() = 30;
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[2], 30);

    const auto& cv = v;
    EXPECT_EQ(cv.front(), 10);
    EXPECT_EQ(cv.back(), 30);
}

// ---------------------------------------------------------------------------
// 27. data
// ---------------------------------------------------------------------------
TEST_CASE(data_pointer) {
    small_vector<int, 4> v = {1, 2, 3};
    int* p = v.data();
    EXPECT_EQ(p[0], 1);
    EXPECT_EQ(p[2], 3);

    const auto& cv = v;
    const int* cp = cv.data();
    EXPECT_EQ(cp[1], 2);
}

// ---------------------------------------------------------------------------
// 28. size/empty/capacity/max_size
// ---------------------------------------------------------------------------
TEST_CASE(size_empty_capacity_max_size) {
    small_vector<int, 4> v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.size(), 0U);
    EXPECT_EQ(v.capacity(), 4U);
    EXPECT_TRUE(v.max_size() > 0U);

    v.push_back(1);
    EXPECT_FALSE(v.empty());
    EXPECT_EQ(v.size(), 1U);
}

// ---------------------------------------------------------------------------
// 29. inlined/inlinable/inline_capacity
// ---------------------------------------------------------------------------
TEST_CASE(inlined_inlinable_inline_capacity) {
    small_vector<int, 3> v;
    EXPECT_TRUE(v.inlined());
    EXPECT_TRUE(v.inlinable());
    EXPECT_EQ(v.inline_capacity(), 3U);

    v = {1, 2, 3};
    EXPECT_TRUE(v.inlined());
    EXPECT_TRUE(v.inlinable());

    v.push_back(4);
    EXPECT_FALSE(v.inlined());
    EXPECT_FALSE(v.inlinable());  // size > inline_capacity

    v.resize(2);
    EXPECT_FALSE(v.inlined());   // still on heap (not yet shrunk)
    EXPECT_TRUE(v.inlinable());  // but could fit inline

    v.shrink_to_fit();
    EXPECT_TRUE(v.inlined());
}

// ---------------------------------------------------------------------------
// 30. iterators
// ---------------------------------------------------------------------------
TEST_CASE(iterators_begin_end) {
    small_vector<int, 4> v = {1, 2, 3, 4};
    int sum = 0;
    for(auto it = v.begin(); it != v.end(); ++it)
        sum += *it;
    EXPECT_EQ(sum, 10);
}

TEST_CASE(iterators_const) {
    const small_vector<int, 4> v = {1, 2, 3};
    int sum = 0;
    for(auto it = v.cbegin(); it != v.cend(); ++it)
        sum += *it;
    EXPECT_EQ(sum, 6);
}

TEST_CASE(iterators_reverse) {
    small_vector<int, 4> v = {1, 2, 3};
    small_vector<int, 4> reversed;
    for(auto it = v.rbegin(); it != v.rend(); ++it)
        reversed.push_back(*it);
    EXPECT_EQ(reversed, (small_vector<int, 4>{3, 2, 1}));
}

TEST_CASE(iterators_const_reverse) {
    const small_vector<int, 4> v = {10, 20, 30};
    auto it = v.crbegin();
    EXPECT_EQ(*it, 30);
    ++it;
    EXPECT_EQ(*it, 20);
    ++it;
    EXPECT_EQ(*it, 10);
    ++it;
    EXPECT_TRUE(it == v.crend());
}

TEST_CASE(iterators_range_for) {
    small_vector<int, 4> v = {1, 2, 3};
    int product = 1;
    for(auto x: v)
        product *= x;
    EXPECT_EQ(product, 6);
}

// ---------------------------------------------------------------------------
// 31. Comparison operators
// ---------------------------------------------------------------------------
TEST_CASE(comparison_equal) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b = {1, 2, 3};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST_CASE(comparison_not_equal_values) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b = {1, 2, 4};
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST_CASE(comparison_not_equal_sizes) {
    small_vector<int, 4> a = {1, 2};
    small_vector<int, 4> b = {1, 2, 3};
    EXPECT_FALSE(a == b);
}

TEST_CASE(comparison_spaceship) {
    small_vector<int, 4> a = {1, 2, 3};
    small_vector<int, 4> b = {1, 2, 4};
    small_vector<int, 4> c = {1, 2, 3};
    small_vector<int, 4> d = {1, 2};

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a <= c);
    EXPECT_TRUE(a >= c);
    EXPECT_TRUE(d < a);  // shorter prefix is less
}

TEST_CASE(comparison_cross_capacity) {
    small_vector<int, 2> a = {1, 2, 3};
    small_vector<int, 8> b = {1, 2, 3};
    EXPECT_TRUE(a == b);

    small_vector<int, 8> c = {1, 2, 4};
    EXPECT_TRUE(a < c);
}

// ---------------------------------------------------------------------------
// 32. Non-member swap
// ---------------------------------------------------------------------------
TEST_CASE(non_member_swap) {
    small_vector<int, 4> a = {1, 2};
    small_vector<int, 4> b = {3, 4, 5};
    swap(a, b);
    EXPECT_EQ(a.size(), 3U);
    EXPECT_EQ(b.size(), 2U);
    EXPECT_EQ(a[0], 3);
    EXPECT_EQ(b[0], 1);
}

// ---------------------------------------------------------------------------
// 33. Non-member erase/erase_if
// ---------------------------------------------------------------------------
TEST_CASE(non_member_erase) {
    small_vector<int, 4> v = {1, 2, 2, 3, 2, 4};
    auto removed = erase(v, 2);
    EXPECT_EQ(removed, 3U);
    EXPECT_EQ(v, (small_vector<int, 4>{1, 3, 4}));
}

TEST_CASE(non_member_erase_if) {
    small_vector<int, 4> v = {1, 2, 3, 4, 5, 6};
    auto removed = erase_if(v, [](int x) { return x % 2 == 0; });
    EXPECT_EQ(removed, 3U);
    EXPECT_EQ(v, (small_vector<int, 4>{1, 3, 5}));
}

// ---------------------------------------------------------------------------
// 34. Non-member begin/end/size/ssize/empty/data
// ---------------------------------------------------------------------------
TEST_CASE(non_member_accessors) {
    small_vector<int, 4> v = {10, 20, 30};

    EXPECT_EQ(*begin(v), 10);
    EXPECT_EQ(*(end(v) - 1), 30);
    EXPECT_EQ(*cbegin(v), 10);

    EXPECT_EQ(size(v), 3U);
    EXPECT_EQ(ssize(v), 3);
    EXPECT_FALSE(empty(v));
    EXPECT_EQ(data(v)[1], 20);

    auto rit = rbegin(v);
    EXPECT_EQ(*rit, 30);
    auto crit = crbegin(v);
    EXPECT_EQ(*crit, 30);

    const auto& cv = v;
    EXPECT_EQ(*begin(cv), 10);
    EXPECT_EQ(*rbegin(cv), 30);
    EXPECT_EQ(data(cv)[0], 10);

    small_vector<int, 4> ev;
    EXPECT_TRUE(empty(ev));
}

// ---------------------------------------------------------------------------
// 35. CTAD
// ---------------------------------------------------------------------------
TEST_CASE(ctad_from_iterators) {
    std::array<int, 4> arr = {1, 2, 3, 4};
    small_vector sv(arr.begin(), arr.end());
    EXPECT_EQ(sv.size(), 4U);
    EXPECT_EQ(sv[0], 1);
    EXPECT_EQ(sv[3], 4);
    // Type should be small_vector<int, N> for some default N.
    static_assert(std::is_same_v<decltype(sv)::value_type, int>);
}

// ---------------------------------------------------------------------------
// 36. Move-only types
// ---------------------------------------------------------------------------
TEST_CASE(move_only_push_emplace) {
    small_vector<move_only, 2> v;
    v.emplace_back(1);
    v.emplace_back(2);
    v.push_back(move_only{3});

    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0].value, 1);
    EXPECT_EQ(v[1].value, 2);
    EXPECT_EQ(v[2].value, 3);
}

TEST_CASE(move_only_move_constructor) {
    small_vector<move_only, 2> a;
    a.emplace_back(10);
    a.emplace_back(20);

    small_vector<move_only, 2> b(std::move(a));
    EXPECT_EQ(b.size(), 2U);
    EXPECT_EQ(b[0].value, 10);
    EXPECT_EQ(b[1].value, 20);
}

TEST_CASE(move_only_move_assignment) {
    small_vector<move_only, 2> a;
    a.emplace_back(5);
    a.emplace_back(6);

    small_vector<move_only, 2> b;
    b.emplace_back(99);
    b = std::move(a);

    EXPECT_EQ(b.size(), 2U);
    EXPECT_EQ(b[0].value, 5);
    EXPECT_EQ(b[1].value, 6);
}

// ---------------------------------------------------------------------------
// 37. Exception safety
// ---------------------------------------------------------------------------
#ifdef __cpp_exceptions

struct throwing_copy {
    inline static int throw_after = -1;
    int value = 0;

    throwing_copy() = default;

    explicit throwing_copy(int v) : value(v) {}

    throwing_copy(const throwing_copy& other) : value(other.value) {
        if(throw_after == 0)
            throw std::runtime_error("copy failed");
        if(throw_after > 0)
            --throw_after;
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

TEST_CASE(at_throws_out_of_range) {
    small_vector<int, 2> v = {1};
    EXPECT_EQ(v.at(0), 1);
    EXPECT_THROWS(v.at(1));
    EXPECT_THROWS(v.at(100));
}

TEST_CASE(push_back_exception_strong_guarantee) {
    small_vector<throwing_copy, 2> v;
    v.push_back(throwing_copy{1});
    v.push_back(throwing_copy{2});

    const int before0 = v[0].value;
    const int before1 = v[1].value;

    throwing_copy::throw_after = 0;
    EXPECT_THROWS(v.push_back(v[0]));
    throwing_copy::throw_after = -1;

    EXPECT_EQ(v.size(), 2U);
    EXPECT_EQ(v[0].value, before0);
    EXPECT_EQ(v[1].value, before1);
}

TEST_CASE(insert_exception_safety) {
    small_vector<throwing_copy, 4> v;
    v.push_back(throwing_copy{1});
    v.push_back(throwing_copy{2});
    v.push_back(throwing_copy{3});

    const auto size_before = v.size();

    throwing_copy val{99};
    throwing_copy::throw_after = 0;
    EXPECT_THROWS(v.insert(v.begin() + 1, val));
    throwing_copy::throw_after = -1;

    // After exception, size should not have increased beyond what it was
    EXPECT_LE(v.size(), size_before + 1);
}

#endif  // __cpp_exceptions

// ---------------------------------------------------------------------------
// 38. append
// ---------------------------------------------------------------------------
TEST_CASE(append_range) {
    small_vector<int, 3> v = {1, 2, 3};
    std::array<int, 3> tail = {4, 5, 6};
    v.append(tail.begin(), tail.end());
    EXPECT_EQ(v, (small_vector<int, 3>{1, 2, 3, 4, 5, 6}));
}

TEST_CASE(append_initializer_list) {
    small_vector<int, 4> v = {1, 2};
    v.append({3, 4, 5});
    EXPECT_EQ(v, (small_vector<int, 4>{1, 2, 3, 4, 5}));
}

TEST_CASE(append_from_small_vector_copy) {
    small_vector<int, 4> src = {4, 5};
    small_vector<int, 4> dst = {1, 2, 3};
    dst.append(src);
    EXPECT_EQ(dst, (small_vector<int, 4>{1, 2, 3, 4, 5}));
    EXPECT_EQ(src.size(), 2U);  // source preserved
}

TEST_CASE(append_from_small_vector_move) {
    small_vector<int, 4> src = {4, 5};
    small_vector<int, 4> dst = {1, 2, 3};
    dst.append(std::move(src));
    EXPECT_EQ(dst, (small_vector<int, 4>{1, 2, 3, 4, 5}));
    EXPECT_TRUE(src.empty());  // source cleared after move
}

// ---------------------------------------------------------------------------
// 39. Trivially copyable types (verify correctness under memcpy optimization)
// ---------------------------------------------------------------------------
TEST_CASE(trivially_copyable_types) {
    static_assert(std::is_trivially_copyable_v<int>);

    small_vector<int, 4> v = {1, 2, 3, 4};
    small_vector<int, 4> copy = v;
    EXPECT_EQ(v, copy);

    small_vector<int, 4> moved = std::move(copy);
    EXPECT_EQ(moved, v);

    // insert in the middle triggers memmove-like paths
    moved.insert(moved.begin() + 2, 99);
    EXPECT_EQ(moved[2], 99);
    EXPECT_EQ(moved[3], 3);
}

// ---------------------------------------------------------------------------
// 40. Non-trivial types (verify proper construction/destruction)
// ---------------------------------------------------------------------------
TEST_CASE(nontrivial_type_lifecycle) {
    nontrivial_alive = 0;
    {
        small_vector<nontrivial, 2> v;
        v.emplace_back(1);
        v.emplace_back(2);
        EXPECT_EQ(nontrivial_alive, 2);

        v.emplace_back(3);  // triggers reallocation
        EXPECT_EQ(nontrivial_alive, 3);
        EXPECT_EQ(v[0].value, 1);
        EXPECT_EQ(v[2].value, 3);

        v.erase(v.begin());
        EXPECT_EQ(nontrivial_alive, 2);

        v.clear();
        EXPECT_EQ(nontrivial_alive, 0);
    }
    EXPECT_EQ(nontrivial_alive, 0);
}

TEST_CASE(nontrivial_copy_and_assign) {
    nontrivial_alive = 0;
    {
        small_vector<nontrivial, 2> a;
        a.emplace_back(10);
        a.emplace_back(20);

        small_vector<nontrivial, 2> b(a);
        EXPECT_EQ(nontrivial_alive, 4);
        EXPECT_EQ(b[0].value, 10);

        small_vector<nontrivial, 2> c;
        c = a;
        EXPECT_EQ(nontrivial_alive, 6);
    }
    EXPECT_EQ(nontrivial_alive, 0);
}

// ---------------------------------------------------------------------------
// 41. Large number of elements: growth beyond inline capacity multiple times
// ---------------------------------------------------------------------------
TEST_CASE(large_growth) {
    small_vector<int, 4> v;
    constexpr int N = 1000;
    for(int i = 0; i < N; ++i)
        v.push_back(i);

    EXPECT_EQ(v.size(), static_cast<std::size_t>(N));
    EXPECT_FALSE(v.inlined());

    for(int i = 0; i < N; ++i)
        EXPECT_EQ(v[static_cast<std::size_t>(i)], i);
}

TEST_CASE(large_growth_with_iota) {
    small_vector<int, 2> v(500);
    std::iota(v.begin(), v.end(), 0);
    EXPECT_EQ(v.size(), 500U);
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[499], 499);
}

// ---------------------------------------------------------------------------
// 42. Empty vector operations
// ---------------------------------------------------------------------------
TEST_CASE(empty_erase_range) {
    small_vector<int, 4> v;
    auto it = v.erase(v.begin(), v.end());
    EXPECT_TRUE(it == v.end());
    EXPECT_TRUE(v.empty());
}

TEST_CASE(empty_insert_at_begin) {
    small_vector<int, 4> v;
    v.insert(v.begin(), 42);
    EXPECT_EQ(v.size(), 1U);
    EXPECT_EQ(v[0], 42);
}

TEST_CASE(empty_insert_range_at_begin) {
    small_vector<int, 4> v;
    std::array<int, 3> arr = {1, 2, 3};
    v.insert(v.begin(), arr.begin(), arr.end());
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 1);
}

TEST_CASE(empty_insert_count_at_begin) {
    small_vector<int, 4> v;
    v.insert(v.begin(), 3, 7);
    EXPECT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 7);
    EXPECT_EQ(v[2], 7);
}

TEST_CASE(empty_swap) {
    small_vector<int, 4> a;
    small_vector<int, 4> b = {1, 2, 3};
    a.swap(b);
    EXPECT_EQ(a.size(), 3U);
    EXPECT_TRUE(b.empty());
}

TEST_CASE(empty_reserve_and_push) {
    small_vector<int, 4> v;
    v.reserve(10);
    EXPECT_TRUE(v.empty());
    EXPECT_GE(v.capacity(), 10U);
    v.push_back(1);
    EXPECT_EQ(v.size(), 1U);
}

TEST_CASE(zero_inline_capacity) {
    small_vector<int, 0> v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.inline_capacity(), 0U);
    EXPECT_TRUE(v.inlined());  // no allocation yet

    v.push_back(1);
    v.push_back(2);
    EXPECT_EQ(v.size(), 2U);
    EXPECT_FALSE(v.inlined());
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
}

};  // TEST_SUITE(common_small_vector)

}  // namespace

}  // namespace eventide
