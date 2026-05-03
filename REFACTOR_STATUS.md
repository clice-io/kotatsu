# Deserializer Refactor Status

## Overview

将 stateful `Deserializer` 类替换为 zero-overhead Backend + Visitor 架构。

设计文档: `refactor-der.md`

## 已完成

### Phase 1+2: JSON Backend (已提交 commit a157e3b)

- 创建 `include/kota/codec/deserialize.h` — `deserialize<Backend>()` 核心类型分发
- 创建 `include/kota/codec/json/backend.h` — `simdjson_backend`
- 创建 `include/kota/codec/json/error_context.h` — thread-local 错误上下文
- 创建 `include/kota/codec/visitors/` 目录:
  - `struct_visitor.h` — struct visitor + attribute dispatch
  - `seq_visitor.h` — vector/array/tuple visitor
  - `map_visitor.h` — map visitor
  - `variant_visitor.h` — external/adjacent/internal tagging
  - `variant_scoring.h` — untagged variant scoring
- 删除旧 `json::Deserializer` 类（~700行）和 `simdjson_source_adapter`
- `from_json()` 完全走新路径
- 实现 re-parse diagnostic 错误路径（字段路径、行列号）
- 测试: clang 1188/1188 通过, debug 1187/1188 (1个 pre-existing failure)

## 进行中 (未提交, 需要验证和修复)

### Phase 3: 其他后端移植

第一个 subagent 做了初步改动但**未经验证**，状态不确定：

#### TOML Backend
- `include/kota/codec/toml/backend.h` (新文件, untracked)
- `include/kota/codec/toml/deserializer.h` (大量删减 -522 行)
- `include/kota/codec/toml/toml.h` (修改)
- **需要**: 实现 `toml_backend` trait, 替换 `from_toml()`, 删除旧 `toml::Deserializer`

#### Content Backend
- `include/kota/codec/content/backend.h` (新文件, untracked)
- `include/kota/codec/content/deserializer.h` (大量删减 -651 行)
- **需要**: 实现 `content_backend` trait, 更新 content API
- **注意**: content 有 `deserialize_traits` 特化 (content::Value, content::Array, content::Object)，需要转换为 `custom_deserialize`

#### Bincode Backend
- `include/kota/codec/bincode/backend.h` (新文件, untracked)
- `include/kota/codec/bincode/deserializer.h` (大量删减 -325 行)
- `include/kota/codec/bincode/bincode.h` (修改)
- `include/kota/ipc/codec/bincode.h` (修改)
- **需要**: 实现 `bincode_backend` trait with `visit_struct_positional`, 无 visit_object

#### 通用改动
- `include/kota/codec/deserialize.h` (+175 行) — 可能加了其他后端需要的通用代码
- `include/kota/codec/visitors/struct_visitor.h` (+82 行) — 可能加了 positional 支持
- 测试文件修改: `content_variant_tests.cpp`, `dom_tests.cpp`

## 未开始

### Phase 4: 清理旧基础设施

- 删除 `detail/deser_dispatch.h` (`StreamingDeserCtx` + `unified_deserialize`)
- 删除 `detail/struct_deserialize.h`
- 删除 `detail/variant_dispatch.h`
- 更新 `detail/codec.h` 中的 `deserialize(d, v)` 公共 API
- 删除 `deserialize_traits`, 统一用 `custom_deserialize`
- 删除 `detail/backend.h` 中的旧 concept (`deserializer_like`, `field_mode`, `can_buffer_raw_field` 等)

## 关键注意事项

1. **`deserialize.h` 中有 simdjson 硬编码**: 泛型的 `deserialize<Backend>()` 在 range check 失败时使用 `simdjson::NUMBER_OUT_OF_RANGE` 和 `simdjson::INCORRECT_TYPE`。Backend trait 提供了 `Backend::type_mismatch` 和 `Backend::number_out_of_range`，但可能没有全部替换干净。移植其他后端前**必须检查并修复**。

2. **`string_match` 已恢复**: struct_visitor 使用编译期优化的 `string_match<table::names>(key)`，不是线性扫描。

3. **error_context 是 JSON 特有的**: `error_context.h` 使用 thread-local 存储错误路径。其他后端需要各自的错误处理方案，或者共享这套机制。

4. **Flatbuffers 不在范围内**: arena backend 架构完全不同，不受此重构影响。

5. **`detail/codec.h` 中的 `codec::deserialize(d, v)`**: 目前仍被 TOML/Content/Bincode 后端使用。Phase 3 完成前不能删除。Phase 4 中统一替换或删除。

## 构建命令

```bash
# Clang release
cmake --build build-cmake-clang -j$(nproc) --target unit_tests
build-cmake-clang/unit_tests --only-failed

# GCC debug
cmake --build build-cmake-debug -j$(nproc) --target unit_tests
build-cmake-debug/unit_tests --only-failed
```
