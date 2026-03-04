#!/usr/bin/env python3
"""Generate modern C++23 LSP headers from an LSP metaModel schema JSON."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request
from collections import Counter, deque
from dataclasses import dataclass, field

# Keep compact form stable; do not run formatter inside this block.
# fmt: off
CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool",
    "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class",
    "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast",
    "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
    "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
    "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast",
    "requires", "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "thread_local", "throw",
    "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
}
# fmt: on

RECURSIVE_ALIASES = ("LSPAny", "LSPArray", "LSPObject")

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
TS_HEADER_INCLUDE = "eventide/language/ts.h"
GENERATED_NAMESPACE = "eventide::jsonrpc::protocol"
SOURCE_TAG = "scripts/lsp_codegen.py"
DEFAULT_SCHEMA_PATH = SCRIPT_DIR / "schema.json"
DEFAULT_FETCH_URL = (
    "https://raw.githubusercontent.com/microsoft/language-server-protocol/"
    "gh-pages/_specifications/lsp/{version}/metaModel/metaModel.json"
)
DEFAULT_FETCH_TIMEOUT = 30.0

SUSPICIOUS_BOOL_PATTERNS = [
    re.compile(r"default(?:s)?\s+to\s+true", re.IGNORECASE),
    re.compile(r"default\s+is\s+true", re.IGNORECASE),
    re.compile(r"true\s+by\s+default", re.IGNORECASE),
    re.compile(r"if\s+omitted[^.\n]*true", re.IGNORECASE),
    re.compile(r"when\s+omitted[^.\n]*true", re.IGNORECASE),
]


@dataclass
class DocInfo:
    documentation: str | None = None
    since: str | None = None
    since_tags: list[str] = field(default_factory=list)
    deprecated: str | None = None
    proposed: bool = False


@dataclass
class PropertyDef:
    name: str
    type_expr: dict
    optional: bool
    doc: DocInfo


@dataclass
class StructDef:
    name: str
    parents: list[str]
    properties: list[PropertyDef]
    doc: DocInfo


@dataclass
class EnumValueDef:
    name: str
    value: str | int
    doc: DocInfo


@dataclass
class EnumDef:
    name: str
    type_expr: dict
    values: list[EnumValueDef]
    supports_custom_values: bool
    doc: DocInfo


@dataclass
class AliasDef:
    name: str
    type_expr: dict
    doc: DocInfo


@dataclass
class RequestDef:
    method: str
    type_name: str | None
    params: dict | None
    result: dict | None
    doc: DocInfo


@dataclass
class NotificationDef:
    method: str
    type_name: str | None
    params: dict | None
    doc: DocInfo


@dataclass
class ExtraParamDef:
    name: str
    method: str


@dataclass
class SchemaModel:
    structures: dict[str, StructDef]
    enumerations: dict[str, EnumDef]
    aliases: dict[str, AliasDef]
    requests: list[RequestDef]
    notifications: list[NotificationDef]


@dataclass
class FlattenedProperty:
    prop: PropertyDef
    declared_in: str


@dataclass
class MemberDef:
    cxx_type: str
    base_name: str
    comments: list[str]
    default_value: str | None


def camel_to_snake(name: str) -> str:
    out: list[str] = []
    for i, c in enumerate(name):
        if c.isupper():
            if i > 0 and (
                name[i - 1].islower() or (i + 1 < len(name) and name[i + 1].islower())
            ):
                out.append("_")
            out.append(c.lower())
        else:
            out.append(c)
    return "".join(out)


def sanitize_identifier(name: str, fallback: str = "value") -> tuple[str, bool]:
    chars = [c if c.isalnum() or c == "_" else "_" for c in name]
    text = "".join(chars).strip("_")
    if not text:
        text = fallback
    if text[0].isdigit():
        text = f"_{text}"

    keyword_hit = text in CPP_KEYWORDS
    if keyword_hit:
        text = f"{text}_"

    return text, keyword_hit


def sanitize_type_identifier(name: str, fallback: str = "Type") -> str:
    chars = [c if c.isalnum() or c == "_" else "_" for c in name]
    text = "".join(chars)
    if not text:
        text = fallback
    if text[0].isdigit():
        text = f"T_{text}"
    if text.startswith("_"):
        text = "Lsp" + text
    if text in CPP_KEYWORDS:
        text = f"{text}_"
    return text


def enum_member_upper_camel(text: str, fallback: str) -> str:
    normalized = re.sub(r"[^0-9A-Za-z_]+", "_", text)
    snake = camel_to_snake(normalized)
    parts = [part for part in snake.split("_") if part]
    if parts:
        candidate = "".join(part[:1].upper() + part[1:] for part in parts)
    else:
        candidate = fallback

    if candidate[0].isdigit():
        candidate = f"V{candidate}"

    if candidate in CPP_KEYWORDS:
        candidate = f"{candidate}_"
    return candidate


def build_name_map(definition_names: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    used: set[str] = set()
    for original in sorted(definition_names):
        base = sanitize_type_identifier(original, fallback="Type")
        candidate = base
        suffix = 2
        while candidate in used:
            candidate = f"{base}_{suffix}"
            suffix += 1
        out[original] = candidate
        used.add(candidate)
    return out


def split_documentation(documentation: str | None) -> list[str]:
    if not documentation:
        return []
    return [line.rstrip() for line in str(documentation).splitlines()]


def documentation_mentions_tag(documentation: str | None, tag: str) -> bool:
    if not documentation:
        return False
    return bool(
        re.search(rf"\b@?{re.escape(tag)}\b", documentation, flags=re.IGNORECASE)
    )


def build_doc_lines(doc: DocInfo) -> list[str]:
    lines = split_documentation(doc.documentation)
    has_since = documentation_mentions_tag(doc.documentation, "since")
    has_since_tags = documentation_mentions_tag(doc.documentation, "sinceTags")
    has_deprecated = documentation_mentions_tag(doc.documentation, "deprecated")
    has_proposed = documentation_mentions_tag(doc.documentation, "proposed")

    def append_tag_line(tag_line: str) -> None:
        for chunk in str(tag_line).splitlines():
            lines.append(chunk.rstrip())

    if doc.since and not has_since:
        append_tag_line(f"@since {doc.since}")
    if doc.since_tags and not has_since and not has_since_tags:
        append_tag_line(f"@sinceTags {', '.join(str(x) for x in doc.since_tags)}")
    if doc.deprecated and not has_deprecated:
        append_tag_line(f"@deprecated {doc.deprecated}")
    if doc.proposed and not has_proposed:
        append_tag_line("@proposed")

    while lines and not lines[-1]:
        lines.pop()
    return lines


def append_doc(out: list[str], indent: str, comments: list[str]) -> None:
    if not comments:
        return
    for line in comments:
        if not line:
            out.append(f"{indent}///")
            continue
        out.append(f"{indent}/// {line}")


def parse_doc(item: dict) -> DocInfo:
    return DocInfo(
        documentation=item.get("documentation"),
        since=item.get("since"),
        since_tags=list(item.get("sinceTags", [])),
        deprecated=item.get("deprecated"),
        proposed=bool(item.get("proposed", False)),
    )


def parse_schema(schema: dict) -> SchemaModel:
    structures: dict[str, StructDef] = {}
    for item in schema.get("structures", []):
        properties: list[PropertyDef] = []
        for prop in item.get("properties", []):
            properties.append(
                PropertyDef(
                    name=prop["name"],
                    type_expr=prop["type"],
                    optional=bool(prop.get("optional", False)),
                    doc=parse_doc(prop),
                )
            )

        parents = [
            x["name"]
            for x in [*item.get("extends", []), *item.get("mixins", [])]
            if x.get("kind") == "reference"
        ]

        structures[item["name"]] = StructDef(
            name=item["name"],
            parents=parents,
            properties=properties,
            doc=parse_doc(item),
        )

    enumerations: dict[str, EnumDef] = {}
    for item in schema.get("enumerations", []):
        values: list[EnumValueDef] = []
        for value in item.get("values", []):
            values.append(
                EnumValueDef(
                    name=str(value.get("name", "value")),
                    value=value.get("value", ""),
                    doc=parse_doc(value),
                )
            )
        enumerations[item["name"]] = EnumDef(
            name=item["name"],
            type_expr=item.get("type", {}),
            values=values,
            supports_custom_values=bool(item.get("supportsCustomValues", False)),
            doc=parse_doc(item),
        )

    aliases: dict[str, AliasDef] = {}
    for item in schema.get("typeAliases", []):
        aliases[item["name"]] = AliasDef(
            name=item["name"],
            type_expr=item["type"],
            doc=parse_doc(item),
        )

    requests: list[RequestDef] = []
    for item in schema.get("requests", []):
        requests.append(
            RequestDef(
                method=item["method"],
                type_name=item.get("typeName"),
                params=item.get("params"),
                result=item.get("result"),
                doc=parse_doc(item),
            )
        )

    notifications: list[NotificationDef] = []
    for item in schema.get("notifications", []):
        notifications.append(
            NotificationDef(
                method=item["method"],
                type_name=item.get("typeName"),
                params=item.get("params"),
                doc=parse_doc(item),
            )
        )

    return SchemaModel(
        structures=structures,
        enumerations=enumerations,
        aliases=aliases,
        requests=requests,
        notifications=notifications,
    )


def fetch_schema(
    output: pathlib.Path,
    *,
    version: str,
) -> dict[str, object]:
    source = DEFAULT_FETCH_URL.format(version=version)

    try:
        with urllib.request.urlopen(source, timeout=DEFAULT_FETCH_TIMEOUT) as response:
            payload = response.read()
    except urllib.error.URLError as exc:
        raise RuntimeError(f"download failed: {exc}") from exc

    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"utf-8 decode failed: {exc}") from exc

    try:
        parsed = json.loads(text)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"invalid json: {exc}") from exc

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")

    return {
        "source": source,
        "bytes": len(payload),
        "output": output,
        "schema_version": parsed.get("metaData", {}).get("version"),
    }


def topological_order(
    nodes: list[tuple[str, str]],
    deps: dict[tuple[str, str], set[tuple[str, str]]],
) -> list[tuple[str, str]]:
    reverse: dict[tuple[str, str], set[tuple[str, str]]] = {n: set() for n in nodes}
    indegree: dict[tuple[str, str], int] = {n: 0 for n in nodes}

    for n in nodes:
        for dep in deps.get(n, set()):
            if dep not in indegree:
                continue
            reverse[dep].add(n)
            indegree[n] += 1

    queue = deque(sorted([n for n in nodes if indegree[n] == 0]))
    ordered: list[tuple[str, str]] = []

    while queue:
        node = queue.popleft()
        ordered.append(node)
        for nxt in sorted(reverse[node]):
            indegree[nxt] -= 1
            if indegree[nxt] == 0:
                queue.append(nxt)

    if len(ordered) != len(nodes):
        existing = set(ordered)
        for node in sorted(nodes):
            if node not in existing:
                ordered.append(node)
    return ordered


def smallest_unsigned_type(max_value: int) -> str:
    if max_value <= 0xFF:
        return "std::uint8_t"
    if max_value <= 0xFFFF:
        return "std::uint16_t"
    if max_value <= 0xFFFFFFFF:
        return "std::uint32_t"
    return "std::uint64_t"


def smallest_signed_type(min_value: int, max_value: int) -> str:
    if min_value >= -(1 << 7) and max_value <= (1 << 7) - 1:
        return "std::int8_t"
    if min_value >= -(1 << 15) and max_value <= (1 << 15) - 1:
        return "std::int16_t"
    if min_value >= -(1 << 31) and max_value <= (1 << 31) - 1:
        return "std::int32_t"
    return "std::int64_t"


def method_to_type_name(method: str, suffix: str) -> str:
    parts = [part for part in re.split(r"[^0-9A-Za-z]+", method) if part]
    if parts:
        base = "".join(part[:1].upper() + part[1:] for part in parts)
    else:
        base = "Method"
    return f"{base}{suffix}"


def derive_params_name(type_name: str | None, method: str) -> str:
    if type_name:
        if type_name.endswith("Request"):
            return f"{type_name[: -len('Request')]}Params"
        if type_name.endswith("Notification"):
            return f"{type_name[: -len('Notification')]}Params"
    return method_to_type_name(method, "Params")


def collect_extra_params(
    requests: list[RequestDef], notifications: list[NotificationDef]
) -> list[ExtraParamDef]:
    def missing_params(
        items: list[RequestDef] | list[NotificationDef],
    ) -> list[ExtraParamDef]:
        return [
            ExtraParamDef(
                name=derive_params_name(item.type_name, item.method),
                method=item.method,
            )
            for item in items
            if item.params is None
        ]

    return [*missing_params(requests), *missing_params(notifications)]


class TypeRenderer:
    def __init__(self, model: SchemaModel, name_map: dict[str, str]):
        self.model = model
        self.name_map = name_map
        self.struct_names = set(model.structures.keys())
        self.alias_names = set(model.aliases.keys())
        self.closed_string_enum_names = {
            name
            for name, enum_def in model.enumerations.items()
            if enum_def.type_expr.get("name") == "string"
            and not enum_def.supports_custom_values
        }
        literal_owner_candidates: dict[str, set[str]] = {}
        for enum_name in self.closed_string_enum_names:
            enum_def = model.enumerations[enum_name]
            for value in enum_def.values:
                literal_text = str(value.value)
                literal_owner_candidates.setdefault(literal_text, set()).add(enum_name)
        self.closed_string_literal_owner = {
            literal: next(iter(owners))
            for literal, owners in literal_owner_candidates.items()
            if len(owners) == 1
        }

    def render_type(
        self, type_expr: dict, owner: str, current_struct: str | None = None
    ) -> str:
        kind = type_expr.get("kind")

        if kind == "base":
            return str(type_expr["name"])

        if kind == "reference":
            ref_name = type_expr["name"]
            if current_struct is not None and ref_name == current_struct:
                cpp_name = self.name_map.get(
                    ref_name, sanitize_type_identifier(ref_name, fallback="Type")
                )
                return f"std::shared_ptr<{cpp_name}>"
            if ref_name in self.closed_string_enum_names:
                enum_cpp = self.name_map.get(
                    ref_name, sanitize_type_identifier(ref_name, fallback="Type")
                )
                return f"enum_string<{enum_cpp}>"
            return self.name_map.get(
                ref_name, sanitize_type_identifier(ref_name, fallback="Type")
            )

        if kind == "array":
            element = self.render_type(
                type_expr["element"],
                owner=f"{owner}.element",
                current_struct=current_struct,
            )
            return f"std::vector<{element}>"

        if kind == "map":
            key = self.render_type(
                type_expr["key"], owner=f"{owner}.key", current_struct=current_struct
            )
            value = self.render_type(
                type_expr["value"],
                owner=f"{owner}.value",
                current_struct=current_struct,
            )
            return f"std::map<{key}, {value}>"

        if kind == "tuple":
            items = [
                self.render_type(
                    item, owner=f"{owner}.tuple_item", current_struct=current_struct
                )
                for item in type_expr.get("items", [])
            ]
            if not items:
                return "std::tuple<>"
            return f"std::tuple<{', '.join(items)}>"

        if kind == "or":
            return self.render_or(
                type_expr.get("items", []), owner=owner, current_struct=current_struct
            )

        if kind == "and":
            items = [
                self.render_type(
                    item, owner=f"{owner}.and_item", current_struct=current_struct
                )
                for item in type_expr.get("items", [])
            ]
            if len(items) == 1:
                return items[0]
            return f"std::tuple<{', '.join(items)}>"

        if kind == "literal":
            return "LspEmptyObject"

        if kind == "stringLiteral":
            literal_value = str(type_expr.get("value", ""))
            owner_enum = self.closed_string_literal_owner.get(literal_value)
            if owner_enum:
                enum_cpp = self.name_map.get(
                    owner_enum, sanitize_type_identifier(owner_enum, fallback="Type")
                )
                return f"enum_string<{enum_cpp}>"
            return "string"

        if kind == "integerLiteral":
            return "integer"

        if kind == "booleanLiteral":
            return "boolean"

        raise ValueError(f"Unsupported type kind: {kind} at {owner}")

    def render_or(
        self, items: list[dict], owner: str, current_struct: str | None = None
    ) -> str:
        rendered: list[str] = []
        saw_null = False

        for item in items:
            if item.get("kind") == "base" and item.get("name") == "null":
                saw_null = True
                continue
            rendered.append(
                self.render_type(
                    item,
                    owner=f"{owner}.or_item",
                    current_struct=current_struct,
                )
            )

        unique: list[str] = []
        seen: set[str] = set()
        for item in rendered:
            if item not in seen:
                seen.add(item)
                unique.append(item)

        if saw_null and len(unique) == 1:
            return f"nullable<{unique[0]}>"
        if saw_null:
            unique = ["null", *unique]

        if not unique:
            return "null"
        if len(unique) == 1:
            return unique[0]
        return f"variant<{', '.join(unique)}>"


class Generator:
    def __init__(self, model: SchemaModel, name_map: dict[str, str]):
        self.model = model
        self.name_map = name_map
        self.renderer = TypeRenderer(model, name_map)

        self.struct_names = set(model.structures.keys())
        self.enum_names = set(model.enumerations.keys())
        self.alias_names = set(model.aliases.keys())

        self.struct_dep_cache: dict[str, set[tuple[str, str]]] = {}
        self.flattened_property_cache: dict[str, list[FlattenedProperty]] = {}

        self.keyword_hits: list[str] = []
        self.bool_default_warnings: list[str] = []
        self.member_collision_warnings: list[str] = []
        self.unsafe_override_warnings: list[str] = []
        self.closed_string_enum_literal_members: dict[str, dict[str, str]] = {}
        for enum_name in self.renderer.closed_string_enum_names:
            enum_def = model.enumerations[enum_name]
            used_member_names: Counter[str] = Counter()
            value_to_member: dict[str, str] = {}
            for index, value in enumerate(enum_def.values):
                base_member_name = enum_member_upper_camel(
                    str(value.value), fallback=f"Value{index + 1}"
                )
                dedupe_index = used_member_names[base_member_name]
                used_member_names[base_member_name] += 1
                member_name = (
                    base_member_name
                    if dedupe_index == 0
                    else f"{base_member_name}{dedupe_index + 1}"
                )
                value_to_member[str(value.value)] = member_name
            self.closed_string_enum_literal_members[enum_name] = value_to_member

    def _walk_type_refs(
        self, type_expr: dict, current_struct: str, out: set[tuple[str, str]]
    ) -> None:
        if not isinstance(type_expr, dict):
            return
        kind = type_expr.get("kind")
        if kind == "reference":
            ref = type_expr.get("name")
            if ref == current_struct:
                return
            if ref in self.struct_names:
                out.add(("S", ref))
            elif ref in self.enum_names:
                out.add(("E", ref))
            elif ref in self.alias_names and ref not in RECURSIVE_ALIASES:
                out.add(("A", ref))
            return

        if kind == "stringLiteral":
            literal_text = str(type_expr.get("value", ""))
            owner_enum = self.renderer.closed_string_literal_owner.get(literal_text)
            if owner_enum in self.enum_names:
                out.add(("E", owner_enum))
            return

        if kind == "array":
            self._walk_type_refs(type_expr.get("element"), current_struct, out)
        elif kind == "map":
            self._walk_type_refs(type_expr.get("key"), current_struct, out)
            self._walk_type_refs(type_expr.get("value"), current_struct, out)
        elif kind in {"or", "and", "tuple"}:
            for item in type_expr.get("items", []):
                self._walk_type_refs(item, current_struct, out)
        elif kind == "literal":
            for prop in type_expr.get("value", {}).get("properties", []):
                self._walk_type_refs(prop.get("type"), current_struct, out)

    def collect_flattened_properties(
        self,
        struct_name: str,
        stack: set[str] | None = None,
    ) -> list[FlattenedProperty]:
        cached = self.flattened_property_cache.get(struct_name)
        if cached is not None:
            return list(cached)

        if stack is None:
            stack = set()
        if struct_name in stack:
            return []
        stack.add(struct_name)

        struct_def = self.model.structures[struct_name]
        out: list[FlattenedProperty] = []

        if len(struct_def.parents) == 1 and struct_def.parents[0] in self.struct_names:
            out.extend(self.collect_flattened_properties(struct_def.parents[0], stack))

        for prop in struct_def.properties:
            out.append(FlattenedProperty(prop=prop, declared_in=struct_name))

        stack.remove(struct_name)
        self.flattened_property_cache[struct_name] = list(out)
        return list(out)

    def struct_dependencies(self, struct_name: str) -> set[tuple[str, str]]:
        cached = self.struct_dep_cache.get(struct_name)
        if cached is not None:
            return set(cached)

        struct_def = self.model.structures[struct_name]
        deps: set[tuple[str, str]] = set()
        if len(struct_def.parents) > 1:
            for parent in struct_def.parents:
                if parent in self.struct_names:
                    deps.add(("S", parent))

        for flat in self.collect_flattened_properties(struct_name):
            self._walk_type_refs(
                flat.prop.type_expr, current_struct=flat.declared_in, out=deps
            )

        deps.discard(("S", struct_name))
        self.struct_dep_cache[struct_name] = set(deps)
        return deps

    def build_node_dependencies(
        self,
    ) -> tuple[list[tuple[str, str]], dict[tuple[str, str], set[tuple[str, str]]]]:
        nodes: list[tuple[str, str]] = []
        nodes.extend(("S", name) for name in self.struct_names)
        nodes.extend(("E", name) for name in self.enum_names)
        nodes.extend(
            ("A", name) for name in self.alias_names if name not in RECURSIVE_ALIASES
        )
        node_set = set(nodes)

        deps: dict[tuple[str, str], set[tuple[str, str]]] = {n: set() for n in nodes}

        for struct_name in self.struct_names:
            node = ("S", struct_name)
            deps[node].update(self.struct_dependencies(struct_name))

        for alias_name, alias_def in self.model.aliases.items():
            if alias_name in RECURSIVE_ALIASES:
                continue
            node = ("A", alias_name)
            alias_deps: set[tuple[str, str]] = set()
            self._walk_type_refs(
                alias_def.type_expr, current_struct=alias_name, out=alias_deps
            )
            alias_deps.discard(node)
            deps[node].update(alias_deps)

        for node, node_deps in list(deps.items()):
            deps[node] = {dep for dep in node_deps if dep in node_set}

        return sorted(nodes), deps

    def build_node_order(self) -> list[tuple[str, str]]:
        nodes, deps = self.build_node_dependencies()
        return topological_order(nodes, deps)

    def is_optional_bool(self, prop: PropertyDef) -> bool:
        return bool(
            prop.optional
            and prop.type_expr.get("kind") == "base"
            and prop.type_expr.get("name") == "boolean"
        )

    def bool_default_needs_warning(self, prop: PropertyDef) -> bool:
        doc = prop.doc.documentation or ""
        if not doc.strip():
            return False
        return any(pattern.search(doc) for pattern in SUSPICIOUS_BOOL_PATTERNS)

    def property_member_name(self, schema_property_name: str) -> tuple[str, bool]:
        snake_name = camel_to_snake(schema_property_name)
        return sanitize_identifier(snake_name, fallback="field")

    def is_type_subtype(self, child: dict, parent: dict) -> bool:
        if child == parent:
            return True

        parent_kind = parent.get("kind")
        child_kind = child.get("kind")

        if parent_kind == "or":
            return any(
                self.is_type_subtype(child, item) for item in parent.get("items", [])
            )

        if child_kind == "or":
            child_items = child.get("items", [])
            return bool(child_items) and all(
                self.is_type_subtype(item, parent) for item in child_items
            )

        if parent_kind == "base":
            parent_name = parent.get("name")
            if child_kind == "base":
                return child.get("name") == parent_name
            if parent_name == "string" and child_kind == "stringLiteral":
                return True
            if parent_name == "integer" and child_kind == "integerLiteral":
                return True
            if parent_name == "uinteger" and child_kind == "integerLiteral":
                value = child.get("value")
                return isinstance(value, int) and value >= 0
            if parent_name == "boolean" and child_kind == "booleanLiteral":
                return True
            return False

        if parent_kind == "stringLiteral":
            return child_kind == "stringLiteral" and child.get("value") == parent.get(
                "value"
            )

        if parent_kind == "integerLiteral":
            return child_kind == "integerLiteral" and child.get("value") == parent.get(
                "value"
            )

        if parent_kind == "booleanLiteral":
            return child_kind == "booleanLiteral" and child.get("value") == parent.get(
                "value"
            )

        if parent_kind == "reference":
            return child_kind == "reference" and child.get("name") == parent.get("name")

        if parent_kind == "array" and child_kind == "array":
            return self.is_type_subtype(
                child.get("element", {}), parent.get("element", {})
            )

        if parent_kind == "map" and child_kind == "map":
            return self.is_type_subtype(
                child.get("key", {}), parent.get("key", {})
            ) and self.is_type_subtype(
                child.get("value", {}),
                parent.get("value", {}),
            )

        if parent_kind == "tuple" and child_kind == "tuple":
            parent_items = parent.get("items", [])
            child_items = child.get("items", [])
            if len(parent_items) != len(child_items):
                return False
            return all(
                self.is_type_subtype(child_item, parent_item)
                for child_item, parent_item in zip(child_items, parent_items)
            )

        return False

    def is_safe_override(
        self, parent_prop: PropertyDef, child_prop: PropertyDef
    ) -> tuple[bool, str]:
        if parent_prop.name != child_prop.name:
            return (
                False,
                f"member-name collision between `{parent_prop.name}` and `{child_prop.name}`",
            )

        if child_prop.optional and not parent_prop.optional:
            return False, "child field is optional while parent field is required"

        if not self.is_type_subtype(child_prop.type_expr, parent_prop.type_expr):
            return False, "child field type is not a safe subtype of parent field type"

        return True, "safe narrowing"

    def make_member(
        self,
        owner_struct: str,
        flat_prop: FlattenedProperty,
        inherited_from: str | None,
    ) -> MemberDef:
        prop = flat_prop.prop
        owner_cpp = self.name_map[owner_struct]
        owner_path = f"{owner_cpp}.{prop.name}"

        rendered_type = self.renderer.render_type(
            prop.type_expr,
            owner=owner_path,
            current_struct=flat_prop.declared_in,
        )

        default_value: str | None = None
        if self.is_optional_bool(prop):
            rendered_type = "optional_bool"
            default_value = "{}"
            if self.bool_default_needs_warning(prop):
                self.bool_default_warnings.append(
                    f"{owner_struct}.{prop.name}: optional bool defaults to false but docs suggest default true."
                )
        elif prop.optional:
            if rendered_type.startswith("variant<") and rendered_type.endswith(">"):
                variant_args = rendered_type[len("variant<") : -1]
                rendered_type = f"optional_variant<{variant_args}>"
            else:
                rendered_type = f"optional<{rendered_type}>"
            default_value = "{}"
        elif prop.type_expr.get("kind") == "stringLiteral":
            literal_text = str(prop.type_expr.get("value", ""))
            owner_enum = self.renderer.closed_string_literal_owner.get(literal_text)
            if owner_enum:
                member_name = self.closed_string_enum_literal_members.get(
                    owner_enum, {}
                ).get(literal_text)
                if member_name:
                    enum_cpp = self.name_map.get(
                        owner_enum,
                        sanitize_type_identifier(owner_enum, fallback="Type"),
                    )
                    default_value = f"{enum_cpp}::{member_name}"

        member_name, keyword_hit = self.property_member_name(prop.name)
        if keyword_hit:
            self.keyword_hits.append(
                f"{owner_struct}.{prop.name}: renamed to `{member_name}` due to C++ keyword collision."
            )

        comments = build_doc_lines(prop.doc)
        if not comments:
            comments = [f"Schema field: {prop.name}."]

        if inherited_from is not None:
            suffix = f"(Inherited from [{inherited_from}])"
            comments[-1] = f"{comments[-1]} {suffix}" if comments else suffix

        return MemberDef(
            cxx_type=rendered_type,
            base_name=member_name,
            comments=comments,
            default_value=default_value,
        )

    def make_flatten_member(self, owner_struct: str, parent_name: str) -> MemberDef:
        parent_cpp = self.name_map.get(
            parent_name, sanitize_type_identifier(parent_name, fallback="Type")
        )
        parent_field_name, keyword_hit = sanitize_identifier(
            camel_to_snake(parent_cpp), fallback="base"
        )
        if keyword_hit:
            self.keyword_hits.append(
                f"{owner_struct}.{parent_name}: renamed flatten field to `{parent_field_name}` due to C++ keyword collision."
            )

        return MemberDef(
            cxx_type=f"flatten<{parent_cpp}>",
            base_name=parent_field_name,
            comments=[],
            default_value=None,
        )

    def collect_struct_members(self, struct_name: str) -> list[MemberDef]:
        struct_def = self.model.structures[struct_name]
        members: list[MemberDef] = []

        if len(struct_def.parents) == 1 and struct_def.parents[0] in self.struct_names:
            parent = struct_def.parents[0]
            local_props_by_member_name: dict[str, PropertyDef] = {}
            for prop in struct_def.properties:
                member_name = self.property_member_name(prop.name)[0]
                local_props_by_member_name[member_name] = prop

            for flat in self.collect_flattened_properties(parent):
                inherited_name = self.property_member_name(flat.prop.name)[0]
                local_prop = local_props_by_member_name.get(inherited_name)
                if local_prop is not None:
                    safe, reason = self.is_safe_override(flat.prop, local_prop)
                    # Single-inheritance override: child field replaces inherited field only when narrowing is safe.
                    if safe:
                        continue

                    self.unsafe_override_warnings.append(
                        f"{struct_name}.{inherited_name}: inherited `{flat.prop.name}` from "
                        f"`{flat.declared_in}` conflicts with local `{local_prop.name}`; {reason}."
                    )
                members.append(
                    self.make_member(struct_name, flat, inherited_from=parent)
                )
        elif len(struct_def.parents) > 1:
            for parent in struct_def.parents:
                if parent not in self.struct_names:
                    continue
                members.append(self.make_flatten_member(struct_name, parent))

        for prop in struct_def.properties:
            members.append(
                self.make_member(
                    struct_name,
                    FlattenedProperty(prop=prop, declared_in=struct_name),
                    inherited_from=None,
                )
            )
        return members

    def unique_member_name(
        self, struct_name: str, base_name: str, used_names: Counter[str]
    ) -> str:
        index = used_names[base_name]
        used_names[base_name] += 1
        if index == 0:
            return base_name
        renamed = f"{base_name}_{index + 1}"
        self.member_collision_warnings.append(
            f"{struct_name}.{base_name}: duplicate member name, renamed to `{renamed}`."
        )
        return renamed

    def emit_alias(self, alias_name: str) -> str:
        alias = self.model.aliases[alias_name]
        alias_cpp = self.name_map[alias_name]
        alias_type = self.renderer.render_type(
            alias.type_expr, owner=f"alias[{alias_name}]"
        )

        lines: list[str] = []
        append_doc(
            lines,
            indent="",
            comments=build_doc_lines(alias.doc),
        )
        lines.append(f"using {alias_cpp} = {alias_type};")
        return "\n".join(lines)

    def emit_struct(self, struct_name: str) -> str:
        struct_def = self.model.structures[struct_name]
        struct_cpp = self.name_map[struct_name]

        lines: list[str] = []
        append_doc(
            lines,
            indent="",
            comments=build_doc_lines(struct_def.doc),
        )
        lines.append(f"struct {struct_cpp} {{")

        members = self.collect_struct_members(struct_name)
        used_names: Counter[str] = Counter()
        if not members:
            lines.append("    // empty")

        for index, member in enumerate(members):
            append_doc(lines, indent="    ", comments=member.comments)
            member_name = self.unique_member_name(
                struct_name, member.base_name, used_names
            )
            decl = f"    {member.cxx_type} {member_name}"
            if member.default_value is not None:
                decl += f" = {member.default_value}"
            decl += ";"
            lines.append(decl)
            if index + 1 < len(members):
                lines.append("")

        lines.append("};")
        return "\n".join(lines)

    def emit_enum(self, enum_name: str) -> str:
        enum_def = self.model.enumerations[enum_name]
        enum_cpp = self.name_map[enum_name]
        base_name = enum_def.type_expr.get("name")

        lines: list[str] = []
        comments = build_doc_lines(enum_def.doc)
        comments.append(
            f"supportsCustomValues: {str(enum_def.supports_custom_values).lower()}"
        )
        append_doc(lines, indent="", comments=comments)

        if base_name in {"integer", "uinteger"}:
            underlying = base_name
            if not enum_def.supports_custom_values:
                values = [v.value for v in enum_def.values if isinstance(v.value, int)]
                if values:
                    if base_name == "integer":
                        underlying = smallest_signed_type(min(values), max(values))
                    else:
                        underlying = smallest_unsigned_type(max(values))
            lines.append(f"enum class {enum_cpp} : {underlying} {{")
            used_member_names: Counter[str] = Counter()
            value_comments_list = [
                build_doc_lines(value.doc) for value in enum_def.values
            ]
            for index, value in enumerate(enum_def.values):
                value_comments = value_comments_list[index]
                append_doc(lines, indent="    ", comments=value_comments)
                base_member_name = enum_member_upper_camel(
                    str(value.name), fallback=f"Value{index + 1}"
                )
                dedupe_index = used_member_names[base_member_name]
                used_member_names[base_member_name] += 1
                member_name = (
                    base_member_name
                    if dedupe_index == 0
                    else f"{base_member_name}{dedupe_index + 1}"
                )
                comma = "," if index + 1 < len(enum_def.values) else ""
                lines.append(f"    {member_name} = {value.value}{comma}")
                if index + 1 < len(enum_def.values) and (
                    value_comments or value_comments_list[index + 1]
                ):
                    lines.append("")
            lines.append("};")
            return "\n".join(lines)

        if base_name == "string":
            if enum_def.supports_custom_values:
                lines.append(f"struct {enum_cpp} : std::string {{")
                lines.append("    using std::string::string;")
                lines.append("    using std::string::operator=;")

                if enum_def.values:
                    lines.append("")

                value_comments_list = [
                    build_doc_lines(value.doc) for value in enum_def.values
                ]
                for index, value in enumerate(enum_def.values):
                    value_comments = value_comments_list[index]
                    append_doc(lines, indent="    ", comments=value_comments)
                    member_name, _ = sanitize_identifier(
                        camel_to_snake(value.name), fallback=f"value_{index}"
                    )
                    escaped = json.dumps(str(value.value))
                    lines.append(
                        f"    constexpr inline static std::string_view {member_name} = {escaped};"
                    )
                    if index + 1 < len(enum_def.values) and (
                        value_comments or value_comments_list[index + 1]
                    ):
                        lines.append("")

                lines.append("};")
                return "\n".join(lines)

            max_value = max(len(enum_def.values) - 1, 0)
            underlying = smallest_unsigned_type(max_value)
            lines.append(f"enum class {enum_cpp} : {underlying} {{")
            used_member_names: Counter[str] = Counter()

            value_comments_list = [
                build_doc_lines(value.doc) for value in enum_def.values
            ]
            for index, value in enumerate(enum_def.values):
                value_comments = value_comments_list[index]
                append_doc(lines, indent="    ", comments=value_comments)

                base_member_name = enum_member_upper_camel(
                    str(value.value), fallback=f"Value{index + 1}"
                )
                dedupe_index = used_member_names[base_member_name]
                used_member_names[base_member_name] += 1
                member_name = (
                    base_member_name
                    if dedupe_index == 0
                    else f"{base_member_name}{dedupe_index + 1}"
                )
                comma = "," if index + 1 < len(enum_def.values) else ""
                lines.append(f"    {member_name}{comma}")
                if index + 1 < len(enum_def.values) and (
                    value_comments or value_comments_list[index + 1]
                ):
                    lines.append("")

            lines.append("};")
            return "\n".join(lines)

        lines.append(f"// Unsupported enum base type: {base_name}")
        return "\n".join(lines)


def emit_extra_param_structs(
    extra_params: list[ExtraParamDef], name_map: dict[str, str]
) -> list[str]:
    blocks: list[str] = []
    for extra in sorted(extra_params, key=lambda item: item.method):
        params_cpp = name_map[extra.name]
        blocks.append(f"struct {params_cpp} {{ }};")
    return blocks


def render_method_params(
    renderer: TypeRenderer,
    name_map: dict[str, str],
    extra_params_by_method: dict[str, ExtraParamDef],
    method: str,
    params: dict | None,
) -> str:
    if params is None:
        extra = extra_params_by_method.get(method)
        if extra is None:
            return "LSPEmpty"
        return name_map[extra.name]
    if not isinstance(params, dict):
        return "LSPEmpty"
    return renderer.render_type(params, owner=f"method[{method}].params")


def sorted_by_method(items: list[RequestDef] | list[NotificationDef]):
    return sorted(items, key=lambda item: item.method)


def build_trait_entries(
    items: list[RequestDef] | list[NotificationDef],
    *,
    result_for,
    renderer: TypeRenderer,
    name_map: dict[str, str],
    extra_params_by_method: dict[str, ExtraParamDef],
) -> list[tuple[str, str] | tuple[str, str, str]]:
    entries: list[tuple[str, str] | tuple[str, str, str]] = []
    for item in sorted_by_method(items):
        params_cpp = render_method_params(
            renderer,
            name_map,
            extra_params_by_method,
            item.method,
            item.params,
        )
        result = result_for(item)
        method = json.dumps(item.method)
        entries.append(
            (params_cpp, method) if result is None else (params_cpp, result, method)
        )
    return entries


def emit_xmacro(
    name: str, entries: list[tuple[str, str] | tuple[str, str, str]]
) -> list[str]:
    lines: list[str] = [f"#define {name}(X) \\"]
    for index, entry in enumerate(entries):
        if len(entry) == 2:
            params_cpp, method = entry
            payload = f"X(({params_cpp}), {method})"
        else:
            params_cpp, result_cpp, method = entry
            payload = f"X(({params_cpp}), ({result_cpp}), {method})"
        suffix = " \\" if index + 1 < len(entries) else ""
        lines.append(f"    {payload}{suffix}")
    return lines


def emit_method_traits(
    generator: Generator,
    requests: list[RequestDef],
    notifications: list[NotificationDef],
    extra_params: list[ExtraParamDef],
    name_map: dict[str, str],
) -> str:
    extra_params_by_method = {extra.method: extra for extra in extra_params}

    def request_result(req: RequestDef) -> str:
        if isinstance(req.result, dict):
            return generator.renderer.render_type(
                req.result, owner=f"method[{req.method}].result"
            )
        return "null"

    lines: list[str] = []
    lines.extend(
        emit_xmacro(
            "LSP_REQUEST_TRAITS_XMACRO",
            build_trait_entries(
                requests,
                result_for=request_result,
                renderer=generator.renderer,
                name_map=name_map,
                extra_params_by_method=extra_params_by_method,
            ),
        )
    )
    lines.append("")
    lines.extend(
        emit_xmacro(
            "LSP_NOTIFICATION_TRAITS_XMACRO",
            build_trait_entries(
                notifications,
                result_for=lambda _: None,
                renderer=generator.renderer,
                name_map=name_map,
                extra_params_by_method=extra_params_by_method,
            ),
        )
    )

    lines.extend(
        [
            "",
            "#define LSP_TRAITS_TYPE(...) __VA_ARGS__",
            "",
            "#define LSP_REQUEST_TRAITS_DECLARE(PARAMS, RESULT, METHOD) \\",
            "template <> \\",
            "struct RequestTraits<LSP_TRAITS_TYPE PARAMS> { \\",
            "    using Result = LSP_TRAITS_TYPE RESULT; \\",
            "    constexpr inline static std::string_view method = METHOD; \\",
            "};",
            "",
            "LSP_REQUEST_TRAITS_XMACRO(LSP_REQUEST_TRAITS_DECLARE)",
            "",
            "#undef LSP_REQUEST_TRAITS_DECLARE",
            "",
            "#define LSP_NOTIFICATION_TRAITS_DECLARE(PARAMS, METHOD) \\",
            "template <> \\",
            "struct NotificationTraits<LSP_TRAITS_TYPE PARAMS> { \\",
            "    constexpr inline static std::string_view method = METHOD; \\",
            "};",
            "",
            "LSP_NOTIFICATION_TRAITS_XMACRO(LSP_NOTIFICATION_TRAITS_DECLARE)",
            "",
            "#undef LSP_NOTIFICATION_TRAITS_DECLARE",
            "#undef LSP_TRAITS_TYPE",
        ]
    )

    return "\n".join(lines)


def generate_protocol_header(
    schema_path: pathlib.Path, output_file: pathlib.Path
) -> dict[str, object]:
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    model = parse_schema(schema)
    extra_params = collect_extra_params(model.requests, model.notifications)

    definition_names = [
        *model.structures.keys(),
        *model.enumerations.keys(),
        *model.aliases.keys(),
        *(extra.name for extra in extra_params),
    ]
    name_map = build_name_map(definition_names)

    generator = Generator(model, name_map)
    node_order = generator.build_node_order()

    emitters = {
        "A": generator.emit_alias,
        "S": generator.emit_struct,
        "E": generator.emit_enum,
    }
    body_blocks = [emitters[kind](name) for kind, name in node_order]

    body_blocks.extend(emit_extra_param_structs(extra_params, name_map))
    body_blocks.append(
        emit_method_traits(
            generator,
            model.requests,
            model.notifications,
            extra_params,
            name_map,
        )
    )

    lines: list[str] = ["#pragma once", ""]
    lines.append(f'#include "{TS_HEADER_INCLUDE}"')
    lines.extend(
        [
            "",
            f"// Generated by {SOURCE_TAG}. DO NOT EDIT.",
            "",
            f"namespace {GENERATED_NAMESPACE} {{",
            "",
        ]
    )

    for idx, block in enumerate(body_blocks):
        if not block:
            continue
        if idx > 0:
            lines.append("")
        lines.append(block.rstrip())

    lines.extend(["", f"}}  // namespace {GENERATED_NAMESPACE}", ""])
    content = "\n".join(lines)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(content.rstrip() + "\n", encoding="utf-8")

    return {
        "struct_count": len(model.structures),
        "enum_count": len(model.enumerations),
        "alias_count": len(model.aliases),
        "output_file": str(output_file),
        "keyword_hits": generator.keyword_hits,
        "bool_warnings": generator.bool_default_warnings,
        "unsafe_overrides": generator.unsafe_override_warnings,
        "member_collisions": generator.member_collision_warnings,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate C++23 LSP headers from an LSP metaModel schema JSON"
    )
    parser.add_argument("--schema", type=pathlib.Path, default=DEFAULT_SCHEMA_PATH)
    parser.add_argument(
        "--version",
        default="3.18",
        help="LSP version folder used when fetching schema (default: %(default)s)",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("include/eventide/language/protocol.h"),
        help="Path to generated protocol header file (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not args.schema.exists():
        try:
            fetch_summary = fetch_schema(output=args.schema, version=args.version)
        except RuntimeError as exc:
            print(f"[fetch_schema] {exc}", file=sys.stderr)
            return 1

        schema_version = fetch_summary.get("schema_version")
        if schema_version:
            print(f"[fetch_schema] schema metaData.version={schema_version}")
        print(f"[fetch_schema] source={fetch_summary['source']}")
        print(
            f"[fetch_schema] wrote {fetch_summary['bytes']} bytes -> {fetch_summary['output']}"
        )

    summary = generate_protocol_header(schema_path=args.schema, output_file=args.output)

    print(f"[codegen] input={args.schema}")
    print(f"[codegen] output_file={summary['output_file']}")
    print(
        "[codegen] counts="
        f" structs={summary['struct_count']}"
        f" enums={summary['enum_count']}"
        f" aliases={summary['alias_count']}"
        " files=1"
    )

    keyword_hits: list[str] = summary["keyword_hits"]  # type: ignore[assignment]
    if keyword_hits:
        for hit in keyword_hits:
            print(f"[INFO] {hit}")
    else:
        print("[INFO] no keyword conflicts detected")

    bool_warnings: list[str] = summary["bool_warnings"]  # type: ignore[assignment]
    if bool_warnings:
        for warning in bool_warnings:
            print(f"[WARNING] {warning}")
    else:
        print("[INFO] no suspicious optional-bool defaults detected")

    unsafe_overrides: list[str] = summary["unsafe_overrides"]  # type: ignore[assignment]
    for warning in unsafe_overrides:
        print(f"[WARNING] {warning}")

    collisions: list[str] = summary["member_collisions"]  # type: ignore[assignment]
    if collisions:
        for collision in collisions:
            print(f"[WARNING] {collision}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
