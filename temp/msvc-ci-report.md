# eventide2 MSVC CI 问题报告

## 背景

本轮 CI 扩容把 Windows 上的 `msvc + /fsanitize=address` 纳入了 CMake matrix。和 Linux/macOS 相比，这条链路暴露出了多处仅在 MSVC 前端或其标准库实现下出现的问题。

相关 PR: `https://github.com/clice-io/eventide/pull/55`

## 问题时间线

### 1. 协程返回对象转换失败

- 现象:
  - `sync.h` 中若干 `task<>` 协程在 MSVC 下报 `cannot convert from 'task_return_object<T,E>' to 'task<void,void,void>'`
- 根因:
  - `task_return_object` 只提供了 `&&` 转换运算符，MSVC 协程代码生成路径会把返回对象当左值处理。
- 修复:
  - 为 `task_return_object` 增加 `&` 版本的转换运算符。
- 相关文件:
  - [include/eventide/async/runtime/task.h](/home/ykiko/C++/eventide2/include/eventide/async/runtime/task.h)

### 2. `with_token()` / `when_any()` 的 `void` 通道在 MSVC 下误实例化成 `optional<void>`

- 现象:
  - `msvc | asan` 在 `peer.cpp` 等文件上报大量 `std::optional<void>` 相关错误。
- 根因:
  - `when_op` 中针对 `winner`、`error`、`cancel` 的值提取逻辑，在 MSVC 下会对本应被 `if constexpr` 排除的 `void` 路径进行更激进的模板实例化。
  - 原始实现依赖 `std::optional<success_type>` / `std::optional<error_type>` 作为中间容器，碰到 `void` 通道时会被 MSVC 错误地展开。
- 修复:
  - 增加 `tuple_visit_at_return(...)`，把 `winner` / `error` / `cancel` 的提取改成直接返回值。
  - 为相关 lambda 加上显式返回类型，避免 MSVC 在 `std::abort()` 分支上把返回类型推成 `void`。
- 相关文件:
  - [include/eventide/async/runtime/when.h](/home/ykiko/C++/eventide2/include/eventide/async/runtime/when.h)

### 3. `refl::type_name` / `enum_name` 作为 `consteval` 在 MSVC ASan 下不可用

- 现象:
  - `serde` / `ipc` 构建中报 `call to immediate function is not a constant expression`
- 根因:
  - `std::source_location::current().function_name()` 这类名字提取逻辑在 MSVC + `/fsanitize=address` 组合下，不能稳定满足 `consteval` 的要求。
- 修复:
  - 将 `type_name()` 和 `enum_name()` 从 `consteval` 放宽为 `constexpr`。
- 相关文件:
  - [include/eventide/reflection/name.h](/home/ykiko/C++/eventide2/include/eventide/reflection/name.h)

### 4. 反射字段名生成时，MSVC 不接受裸指针 NTTP

- 现象:
  - `flatbuffers_torture_tests.cpp` / `toml_torture_tests.cpp` / `bincode_torture_tests.cpp` 报
    `pointer_name: no matching overloaded function found`
  - 具体提示为 `expected compile-time constant expression`
- 根因:
  - `reflection<Object>::field_names` 中直接用 `pointer_name<std::get<Is>(addrs)>()`。
  - 在 MSVC ASan 下，这个裸指针非类型模板参数不再被接受为稳定常量表达式。
  - `name.h` 里其实已经有 `detail::wrapper` 作为 MSVC 兼容层，但调用点之前没有显式使用。
- 修复:
  - 改成 `pointer_name<detail::wrapper{std::get<Is>(addrs)}>()`。
- 相关文件:
  - [include/eventide/reflection/struct.h](/home/ykiko/C++/eventide2/include/eventide/reflection/struct.h)
  - [include/eventide/reflection/name.h](/home/ykiko/C++/eventide2/include/eventide/reflection/name.h)

## 总结

本轮 MSVC 问题的共同特征是:

- MSVC 对模板未选分支和返回类型推导更保守，容易把本应被 `if constexpr` 排除的路径也实例化出来。
- MSVC 在协程返回对象和 `std::source_location` 相关常量求值上的行为，与 Clang/GCC 明显不同。
- `/fsanitize=address` 会进一步放大 NTTP 和常量表达式相关的不兼容。

因此这类代码后续建议遵循两个原则:

- 不要在跨编译器热点模板里依赖“编译器应该能消掉的 `void` 分支”。
- 涉及 NTTP、`consteval`、协程返回对象的地方，优先写成对 MSVC 也显式成立的形式。
