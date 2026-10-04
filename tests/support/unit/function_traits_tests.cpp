#include <string>
#include <tuple>

#include "kota/zest/zest.h"
#include "kota/support/function_traits.h"

namespace kota {

namespace {

struct Widget {
    int size = 0;

    double scale(int factor, float bias);
    std::string name() const;
    std::string take() &&;
    int peek() const& noexcept;
};

struct Functor {
    bool operator()(const std::string&) const;
};

struct Overloaded {
    void operator()(int);
    void operator()(double);
};

ZEST_SUITE(support_function_traits) {

ZEST_CASE(function_types_give_the_return_and_arguments) {
    ZEXPECT(zest::type_eq<function_return_t<int(char, long&)>, int>());
    ZEXPECT(zest::type_eq<function_args_t<int(char, long&)>, std::tuple<char, long&>>());
    ZSTATIC_EXPECT(function_args_count_v<int(char, long&)> == 2U);
    ZEXPECT(zest::type_eq<function_return_t<void() const & noexcept>, void>());
    ZSTATIC_EXPECT(function_args_count_v<void(int) & noexcept> == 1U);
    ZEXPECT(zest::type_eq<function_traits<int(char) const & noexcept>::function_type, int(char)>());
}

ZEST_CASE(object_type_follows_the_qualifiers) {
    // Each of the twelve forms, with and without noexcept.
    ZEXPECT(zest::type_eq<function_traits<void()>::object_type<Widget>, Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() &>::object_type<Widget>, Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() &&>::object_type<Widget>, Widget&&>());
    ZEXPECT(zest::type_eq<function_traits<void() const>::object_type<Widget>, const Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() const&>::object_type<Widget>, const Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() const&&>::object_type<Widget>, const Widget&&>());
    ZEXPECT(zest::type_eq<function_traits<void() noexcept>::object_type<Widget>, Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() & noexcept>::object_type<Widget>, Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() && noexcept>::object_type<Widget>, Widget&&>());
    ZEXPECT(zest::type_eq<function_traits<void() const noexcept>::object_type<Widget>,
                          const Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() const & noexcept>::object_type<Widget>,
                          const Widget&>());
    ZEXPECT(zest::type_eq<function_traits<void() const && noexcept>::object_type<Widget>,
                          const Widget&&>());
}

ZEST_CASE(member_pointers_give_the_member_and_class) {
    ZEXPECT(zest::type_eq<member_type_t<int Widget::*>, int>());
    ZEXPECT(zest::type_eq<class_type_t<int Widget::*>, Widget>());
    ZEXPECT(zest::type_eq<class_type_t<decltype(&Widget::scale)>, Widget>());
}

ZEST_CASE(callables_of_each_kind) {
    using pointer = int (*)(char, long&);
    ZSTATIC_EXPECT(is_function_pointer_v<pointer>);
    ZEXPECT(zest::type_eq<callable_return_t<pointer>, int>());
    ZEXPECT(zest::type_eq<callable_args_t<pointer>, std::tuple<char, long&>>());

    using method = decltype(&Widget::scale);
    ZEXPECT(zest::type_eq<callable_return_t<method>, double>());
    // A member function takes its object first.
    ZEXPECT(zest::type_eq<callable_args_t<method>, std::tuple<Widget&, int, float>>());
    ZSTATIC_EXPECT(callable_args_count_v<method> == 3U);
    ZEXPECT(zest::type_eq<callable_args_t<decltype(&Widget::name)>, std::tuple<const Widget&>>());
    ZEXPECT(zest::type_eq<callable_args_t<decltype(&Widget::take)>, std::tuple<Widget&&>>());
    ZEXPECT(zest::type_eq<callable_args_t<decltype(&Widget::peek)>, std::tuple<const Widget&>>());
    ZEXPECT(zest::type_eq<callable_return_t<decltype(&Widget::peek)>, int>());

    ZSTATIC_EXPECT(is_functor_v<Functor>);
    ZEXPECT(zest::type_eq<callable_return_t<Functor>, bool>());
    ZEXPECT(zest::type_eq<callable_args_t<Functor>, std::tuple<const std::string&>>());

    auto lambda = [](int, int) {
        return 'c';
    };
    ZEXPECT(zest::type_eq<callable_return_t<decltype(lambda)>, char>());
    ZSTATIC_EXPECT(callable_args_count_v<decltype(lambda)> == 2U);
}

ZEST_CASE(only_a_unique_call_operator_makes_a_functor) {
    ZSTATIC_EXPECT(!is_functor_v<Overloaded>);
    ZSTATIC_EXPECT(!is_functor_v<int>);
    auto generic = [](auto) {
    };
    ZSTATIC_EXPECT(!is_functor_v<decltype(generic)>);
}

ZEST_CASE(tuple_push_front_prepends) {
    ZEXPECT(zest::type_eq<tuple_push_front_t<int, std::tuple<char, bool>>,
                          std::tuple<int, char, bool>>());
    ZEXPECT(zest::type_eq<tuple_push_front_t<int, std::tuple<>>, std::tuple<int>>());
}

};  // ZEST_SUITE(support_function_traits)

}  // namespace

}  // namespace kota
