#!/usr/bin/env python3
"""Generate include/kota/ipc/lsp/protocol.h from the LSP metaModel.

The metaModel is pinned to one commit of microsoft/language-server-protocol,
so regenerating is reproducible; bump SCHEMA_COMMIT to follow the spec.
`--check` regenerates in memory and fails when the committed header differs.

Mapping from the metaModel's TypeScript constructs to C++ (vocabulary in
kota/ipc/lsp/ts.h):

- structures become aggregates with every inherited property (`extends` and
  `mixins`) inlined, so members are reached and designated directly; a
  property a structure redeclares narrows the inherited one in place.
- `LSPAny` / `LSPObject` / `LSPArray` alias the codec's dynamic value types.
- `T | null` is `nullable<T>` (over a `variant` for several alternatives), an
  optional property is `optional<T>`, an optional boolean is `optional_bool`
  (absent reads as false), and an optional property holding its own structure
  is `optional_ptr<T>`.
- a string literal type is `Literal<"...">`, which decodes only its own text
  so untagged variants tell their alternatives apart by it.
- enumerations keep unknown values: integer ones are `enum class` over the
  full integer, string ones wrap `std::string` with named constants.
- every request / notification gets a `RequestTraits` / `NotificationTraits`
  specialization keyed by its params type; methods without params get an
  empty params structure so the key stays unique.
"""

import argparse
import functools
import graphlib
import json
import pathlib
import re
import sys
import urllib.request

SCHEMA_VERSION = "3.18"
SCHEMA_COMMIT = "b7f5132c95261c0898ae5124e7a91707abc48fcd"
SCHEMA_URL = (
    "https://raw.githubusercontent.com/microsoft/language-server-protocol/"
    f"{SCHEMA_COMMIT}/_specifications/lsp/{SCHEMA_VERSION}/metaModel/metaModel.json"
)

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCHEMA_PATH = ROOT / ".cache" / "lsp" / f"metaModel-{SCHEMA_COMMIT}.json"
OUTPUT_PATH = ROOT / "include" / "kota" / "ipc" / "lsp" / "protocol.h"

# Named after the metaModel; the aliases live in kota/ipc/protocol.h and ts.h.
BASE_TYPES = {
    "boolean",
    "integer",
    "uinteger",
    "decimal",
    "string",
    "null",
    "URI",
    "DocumentUri",
}
DYNAMIC_TYPES = {
    "LSPAny": "kota::codec::dyn::Value",
    "LSPObject": "kota::codec::dyn::Object",
    "LSPArray": "kota::codec::dyn::Array",
}

# An optional boolean becomes `optional_bool`, which reads absence as false;
# a property documented to default to true would silently flip.
DEFAULT_TRUE = re.compile(r"defaults? (?:to|is) true|true by default", re.IGNORECASE)

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


class SchemaError(Exception):
    """The metaModel uses a construct this generator does not map."""


def identifier(name: str) -> str:
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) or name in CPP_KEYWORDS:
        raise SchemaError(f"`{name}` is not usable as a C++ identifier")
    return name


def member_name(property_name: str) -> str:
    """snake_case member whose lower_camel rename (ipc::lsp_config) restores
    the property name."""
    snake = re.sub(
        r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", property_name
    )
    snake = identifier(snake.lower())
    head, *rest = snake.split("_")
    if head + "".join(part.capitalize() for part in rest) != property_name:
        raise SchemaError(f"property `{property_name}` does not survive the rename")
    return snake


def constant_name(value_name: str) -> str:
    return identifier(value_name[:1].upper() + value_name[1:])


def doc_comment(item: dict, indent: str = "") -> list[str]:
    # The documentation already carries its @since / @deprecated tags.
    lines = item.get("documentation", "").splitlines()
    if any(line.endswith("\\") for line in lines):
        # A trailing backslash would splice the next line into the comment.
        raise SchemaError(f"documentation of `{item['name']}` ends a line in `\\`")
    return [f"{indent}/// {line}".rstrip() for line in lines]


def separated(blocks: list[list[str]]) -> list[str]:
    """Blocks joined by blank lines."""
    out: list[str] = []
    for block in blocks:
        if out:
            out.append("")
        out += block
    return out


def params_name(message: dict) -> str:
    """Name of the empty params structure of a method that takes none."""
    type_name = message["typeName"]
    for suffix in ("Request", "Notification"):
        if type_name.endswith(suffix):
            return type_name.removesuffix(suffix) + "Params"
    raise SchemaError(f"method `{message['method']}` has an unexpected type name")


class Generator:
    def __init__(self, schema: dict):
        self.structures = {item["name"]: item for item in schema["structures"]}
        self.enumerations = {item["name"]: item for item in schema["enumerations"]}
        self.aliases = {item["name"]: item for item in schema["typeAliases"]}
        self.requests = sorted(schema["requests"], key=lambda item: item["method"])
        self.notifications = sorted(
            schema["notifications"], key=lambda item: item["method"]
        )

        for message in [*self.requests, *self.notifications]:
            if "params" not in message:
                name = params_name(message)
                if name in self.structures:
                    raise SchemaError(f"params structure `{name}` already exists")
                self.structures[name] = {
                    "name": name,
                    "documentation": f"Params of `{message['method']}`, which takes none.",
                }
                message["params"] = {"kind": "reference", "name": name}

    @functools.cache
    def properties(self, name: str) -> list[dict]:
        structure = self.structures[name]
        merged: dict[str, dict] = {}
        for parent in [*structure.get("extends", []), *structure.get("mixins", [])]:
            for prop in self.properties(parent["name"]):
                if merged.get(prop["name"], prop) != prop:
                    raise SchemaError(f"{name}: parents disagree on `{prop['name']}`")
                merged[prop["name"]] = prop
        for prop in structure.get("properties", []):
            inherited = merged.get(prop["name"])
            # The one redeclaration the spec makes narrows a string to a
            # literal (`ResourceOperation.kind` in CreateFile and friends).
            if inherited is not None and not (
                inherited["type"] == {"kind": "base", "name": "string"}
                and prop["type"]["kind"] == "stringLiteral"
                and prop.get("optional") == inherited.get("optional")
            ):
                raise SchemaError(f"{name}.{prop['name']} does not narrow its parent's")
            # Reassigning an existing key keeps its position: the narrowed
            # property stays where the parent declared it.
            merged[prop["name"]] = prop
        return list(merged.values())

    def render(self, t: dict) -> str:
        match t["kind"]:
            case "base":
                if t["name"] not in BASE_TYPES:
                    raise SchemaError(f"unknown base type `{t['name']}`")
                return t["name"]
            case "reference":
                name = t["name"]
                if name.startswith("_") or not (
                    name in self.structures
                    or name in self.enumerations
                    or name in self.aliases
                ):
                    raise SchemaError(f"unusable reference `{name}`")
                return name
            case "array":
                return f"std::vector<{self.render(t['element'])}>"
            case "map":
                return f"std::map<{self.render(t['key'])}, {self.render(t['value'])}>"
            case "tuple":
                return (
                    f"std::tuple<{', '.join(self.render(item) for item in t['items'])}>"
                )
            case "or":
                null = {"kind": "base", "name": "null"}
                alternatives = [
                    self.render(item) for item in t["items"] if item != null
                ]
                rendered = (
                    alternatives[0]
                    if len(alternatives) == 1
                    else f"variant<{', '.join(alternatives)}>"
                )
                return f"nullable<{rendered}>" if null in t["items"] else rendered
            case "literal":
                if t["value"]["properties"]:
                    raise SchemaError(
                        "object literal types with properties are unsupported"
                    )
                return "EmptyObject"
            case "stringLiteral":
                return f"Literal<{json.dumps(t['value'])}>"
            case kind:
                raise SchemaError(f"unsupported type kind `{kind}`")

    def dependencies(self, t: dict) -> set[str]:
        """Structures and aliases a rendered type names."""
        match t["kind"]:
            case "reference":
                return set() if t["name"] in self.enumerations else {t["name"]}
            case "array":
                return self.dependencies(t["element"])
            case "map":
                return self.dependencies(t["key"]) | self.dependencies(t["value"])
            case "or" | "tuple":
                return set().union(*(self.dependencies(item) for item in t["items"]))
            case _:
                return set()

    def member(self, owner: str, prop: dict) -> list[str]:
        name = member_name(prop["name"])
        t = prop["type"]
        if t == {"kind": "reference", "name": owner}:
            # The structure contains itself (`SelectionRange.parent`); a
            # by-value member would make it infinite.
            if not prop.get("optional"):
                raise SchemaError(f"{owner}.{prop['name']} requires itself")
            declaration = f"optional_ptr<{owner}> {name} = {{}};"
        elif prop.get("optional") and t == {"kind": "base", "name": "boolean"}:
            if DEFAULT_TRUE.search(prop.get("documentation", "")):
                raise SchemaError(f"{owner}.{prop['name']} defaults to true")
            declaration = f"optional_bool {name} = {{}};"
        elif prop.get("optional"):
            declaration = f"optional<{self.render(t)}> {name} = {{}};"
        elif t["kind"] == "stringLiteral":
            declaration = f"{self.render(t)} {name} = {{}};"
        else:
            declaration = f"{self.render(t)} {name};"
        return [*doc_comment(prop, "    "), f"    {declaration}"]

    def emit_structure(self, name: str) -> list[str]:
        head = doc_comment(self.structures[name])
        members = [self.member(name, prop) for prop in self.properties(name)]
        name = identifier(name)
        if not members:
            return [*head, f"struct {name} {{}};"]
        return [*head, f"struct {name} {{", *separated(members), "};"]

    def emit_alias(self, name: str) -> list[str]:
        target = DYNAMIC_TYPES.get(name) or self.render(self.aliases[name]["type"])
        return [
            *doc_comment(self.aliases[name]),
            f"using {identifier(name)} = {target};",
        ]

    def emit_enumeration(self, name: str) -> list[str]:
        enumeration = self.enumerations[name]
        name = identifier(name)
        base = enumeration["type"]["name"]
        spaced = any("documentation" in value for value in enumeration["values"])

        def entries(declare) -> list[str]:
            out: list[str] = []
            for value in enumeration["values"]:
                if spaced and out:
                    out.append("")
                constant = constant_name(value["name"])
                out += [*doc_comment(value, "    "), f"    {declare(constant, value)}"]
            return out

        def integer_entry(constant: str, value: dict) -> str:
            return f"{constant} = {value['value']},"

        def string_entry(constant: str, value: dict) -> str:
            text = json.dumps(value["value"])
            return f"constexpr static std::string_view {constant} = {text};"

        head = doc_comment(enumeration)
        if base in ("integer", "uinteger"):
            # Declared over the full integer so values newer peers send decode
            # instead of failing the whole message.
            return [
                *head,
                f"enum class {name} : {base} {{",
                *entries(integer_entry),
                "};",
            ]
        if base != "string":
            raise SchemaError(f"enumeration `{name}` over `{base}`")
        return [
            *head,
            f"struct {name} : std::string {{",
            "    using std::string::string;",
            "    using std::string::operator=;",
            "",
            f"    {name}() = default;",
            "",
            # Implicit, so the constants below initialize the type directly
            # (std::string's string_view constructor is explicit).
            f"    {name}(std::string_view value) : std::string(value) {{}}",
            "",
            *entries(string_entry),
            "};",
        ]

    def emit_traits(self) -> list[str]:
        blocks: list[list[str]] = []
        for kind, messages in (
            ("RequestTraits", self.requests),
            ("NotificationTraits", self.notifications),
        ):
            keys = [self.render(message["params"]) for message in messages]
            if len(set(keys)) != len(keys):
                raise SchemaError(f"two methods share a {kind} params type")
            for message, key in zip(messages, keys):
                method = json.dumps(message["method"])
                block = ["template <>", f"struct {kind}<{key}> {{"]
                if "result" in message:
                    block.append(
                        f"    using Result = {self.render(message['result'])};"
                    )
                block += [
                    f"    constexpr static std::string_view method = {method};",
                    "};",
                ]
                blocks.append(block)
        return separated(blocks)

    def ordered_declarations(self) -> list[str]:
        """Aliases and structures, each after every other one it names."""
        graph = {
            name: set() if name in DYNAMIC_TYPES else self.dependencies(alias["type"])
            for name, alias in self.aliases.items()
        }
        # Structures prefixed with `_` exist only to be inherited
        # (`_InitializeParams`); their properties are inlined into the
        # children and they are not emitted.
        for name in self.structures:
            if name.startswith("_"):
                continue
            graph[name] = set().union(
                *(self.dependencies(prop["type"]) for prop in self.properties(name))
            )
            # A structure may name itself: inside std::vector, which admits the
            # incomplete type, or through `optional_ptr` (see member()).
            graph[name].discard(name)

        sorter = graphlib.TopologicalSorter(graph)
        sorter.prepare()
        order: list[str] = []
        while sorter.is_active():
            ready = sorted(sorter.get_ready())
            order += ready
            sorter.done(*ready)
        return order

    def generate(self) -> str:
        blocks = [self.emit_enumeration(name) for name in sorted(self.enumerations)]
        for name in self.ordered_declarations():
            blocks.append(
                self.emit_alias(name)
                if name in self.aliases
                else self.emit_structure(name)
            )
        blocks.append(self.emit_traits())

        lines = [
            "#pragma once",
            "",
            f"// Generated by scripts/lsp_codegen.py from the LSP {SCHEMA_VERSION} metaModel at",
            f"// microsoft/language-server-protocol@{SCHEMA_COMMIT}. DO NOT EDIT.",
            "",
            '#include "kota/ipc/lsp/ts.h"',
            "",
            "namespace kota::ipc::protocol {",
        ]
        lines += ["", *separated(blocks), "", "}  // namespace kota::ipc::protocol", ""]
        return "\n".join(lines)


def load_schema() -> dict:
    if not SCHEMA_PATH.exists():
        print(f"fetching {SCHEMA_URL}", file=sys.stderr)
        with urllib.request.urlopen(SCHEMA_URL, timeout=60) as response:
            payload = response.read()
        SCHEMA_PATH.parent.mkdir(parents=True, exist_ok=True)
        # Written aside and renamed, so an interrupted write never leaves a
        # truncated cache that later runs would trust.
        partial = SCHEMA_PATH.with_suffix(".partial")
        partial.write_bytes(payload)
        partial.replace(SCHEMA_PATH)
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail when the committed header differs from what would be generated",
    )
    args = parser.parse_args()

    header = Generator(load_schema()).generate()
    if args.check:
        if OUTPUT_PATH.read_text(encoding="utf-8") != header:
            print(
                f"{OUTPUT_PATH} is stale; run `pixi run lsp-codegen`", file=sys.stderr
            )
            return 1
        return 0
    OUTPUT_PATH.write_text(header, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
