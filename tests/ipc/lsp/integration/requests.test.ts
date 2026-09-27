// Each request the stub serves, answered with the stub's result.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  CodeActionKind,
  CodeActionRequest,
  CodeLensRequest,
  Command,
  CompletionRequest,
  DeclarationRequest,
  DefinitionRequest,
  DocumentFormattingRequest,
  DocumentHighlightKind,
  DocumentHighlightRequest,
  DocumentLinkRequest,
  DocumentRangeFormattingRequest,
  DocumentSymbol,
  DocumentSymbolRequest,
  FoldingRangeRequest,
  HoverRequest,
  ImplementationRequest,
  InlayHintKind,
  InlayHintRequest,
  Location,
  MarkupKind,
  PrepareRenameRequest,
  ReferencesRequest,
  RenameRequest,
  SelectionRangeRequest,
  SignatureHelpRequest,
  SymbolKind,
  TypeDefinitionRequest,
  WorkspaceEdit,
  WorkspaceSymbolRequest,
  type Definition,
  type DefinitionLink,
} from "vscode-languageserver-protocol";

import { withStub } from "./stub_client.ts";

const TEST_URI = "file:///tmp/test.cpp";
const DOCUMENT = { uri: TEST_URI };
const POSITION = { line: 0, character: 0 };
const FORMATTING = { tabSize: 4, insertSpaces: true };

// The one location the stub answers a definition-like request with.
function location(result: Definition | DefinitionLink[] | null) {
  const first = Array.isArray(result) ? result[0] : result;
  assert.ok(Location.is(first));
  return first;
}

test(
  "hover_returns_markup",
  withStub(async (stub) => {
    const hover = await stub.connection.sendRequest(HoverRequest.type, {
      textDocument: DOCUMENT,
      position: POSITION,
    });
    assert.deepEqual(hover?.contents, {
      kind: MarkupKind.Markdown,
      value: "stub hover",
    });
  }),
);

test(
  "completion_returns_list",
  withStub(async (stub) => {
    const list = await stub.connection.sendRequest(CompletionRequest.type, {
      textDocument: DOCUMENT,
      position: POSITION,
    });
    assert.ok(list && !Array.isArray(list));
    assert.equal(list.isIncomplete, false);
    assert.equal(list.items[0].label, "stub_item");
  }),
);

test(
  "definition_returns_location",
  withStub(async (stub) => {
    const definition = location(
      await stub.connection.sendRequest(DefinitionRequest.type, {
        textDocument: DOCUMENT,
        position: POSITION,
      }),
    );
    assert.equal(definition.uri, TEST_URI);
    assert.equal(definition.range.start.line, 10);
  }),
);

test(
  "references_returns_locations",
  withStub(async (stub) => {
    const references = await stub.connection.sendRequest(
      ReferencesRequest.type,
      {
        textDocument: DOCUMENT,
        position: POSITION,
        context: { includeDeclaration: true },
      },
    );
    assert.ok(references);
    const lines = references
      .map(({ range }) => range.start.line)
      .toSorted((a, b) => a - b);
    assert.deepEqual(lines, [1, 5]);
  }),
);

test(
  "document_symbol_returns_symbols",
  withStub(async (stub) => {
    const symbols = await stub.connection.sendRequest(
      DocumentSymbolRequest.type,
      {
        textDocument: DOCUMENT,
      },
    );
    assert.ok(symbols && symbols.length >= 1);
    const [symbol] = symbols;
    assert.ok(DocumentSymbol.is(symbol));
    assert.equal(symbol.name, "StubSymbol");
    assert.equal(symbol.kind, SymbolKind.Function);
  }),
);

test(
  "formatting_returns_edits",
  withStub(async (stub) => {
    const edits = await stub.connection.sendRequest(
      DocumentFormattingRequest.type,
      {
        textDocument: DOCUMENT,
        options: FORMATTING,
      },
    );
    assert.ok(edits && edits.length >= 1);
    assert.equal(edits[0].newText, "formatted\n");
  }),
);

test(
  "code_action_returns_actions",
  withStub(async (stub) => {
    const actions = await stub.connection.sendRequest(CodeActionRequest.type, {
      textDocument: DOCUMENT,
      range: { start: POSITION, end: { line: 0, character: 5 } },
      context: { diagnostics: [] },
    });
    assert.ok(actions && actions.length >= 1);
    const [action] = actions;
    assert.ok(!Command.is(action));
    assert.equal(action.title, "stub action");
    assert.equal(action.kind, CodeActionKind.QuickFix);
  }),
);

test(
  "signature_help_returns_signature",
  withStub(async (stub) => {
    const help = await stub.connection.sendRequest(SignatureHelpRequest.type, {
      textDocument: DOCUMENT,
      position: POSITION,
    });
    assert.equal(help?.signatures.length, 1);
    const [signature] = help.signatures;
    assert.equal(signature.label, "void foo(int x)");
    assert.equal(signature.parameters?.length, 1);
  }),
);

test(
  "document_highlight_returns_highlight",
  withStub(async (stub) => {
    const highlights = await stub.connection.sendRequest(
      DocumentHighlightRequest.type,
      {
        textDocument: DOCUMENT,
        position: POSITION,
      },
    );
    assert.equal(highlights?.length, 1);
    assert.equal(highlights[0].kind, DocumentHighlightKind.Read);
    assert.equal(highlights[0].range.start.line, 0);
  }),
);

test(
  "rename_returns_workspace_edit",
  withStub(async (stub) => {
    const edit = await stub.connection.sendRequest(RenameRequest.type, {
      textDocument: DOCUMENT,
      position: POSITION,
      newName: "new_name",
    });
    assert.ok(WorkspaceEdit.is(edit));
    const edits = edit.changes?.[TEST_URI];
    assert.equal(edits?.length, 1);
    assert.equal(edits[0].newText, "new_name");
  }),
);

test(
  "prepare_rename_returns_range",
  withStub(async (stub) => {
    const range = await stub.connection.sendRequest(PrepareRenameRequest.type, {
      textDocument: DOCUMENT,
      position: POSITION,
    });
    assert.notEqual(range, null);
  }),
);

test(
  "folding_range_returns_range",
  withStub(async (stub) => {
    const ranges = await stub.connection.sendRequest(FoldingRangeRequest.type, {
      textDocument: DOCUMENT,
    });
    assert.equal(ranges?.length, 1);
    assert.equal(ranges[0].startLine, 0);
    assert.equal(ranges[0].endLine, 10);
  }),
);

test(
  "selection_range_returns_range",
  withStub(async (stub) => {
    const ranges = await stub.connection.sendRequest(
      SelectionRangeRequest.type,
      {
        textDocument: DOCUMENT,
        positions: [POSITION],
      },
    );
    assert.equal(ranges?.length, 1);
    assert.equal(ranges[0].range.start.line, 0);
  }),
);

test(
  "declaration_returns_location",
  withStub(async (stub) => {
    const declaration = location(
      await stub.connection.sendRequest(DeclarationRequest.type, {
        textDocument: DOCUMENT,
        position: POSITION,
      }),
    );
    assert.equal(declaration.range.start.line, 5);
  }),
);

test(
  "type_definition_returns_location",
  withStub(async (stub) => {
    const definition = location(
      await stub.connection.sendRequest(TypeDefinitionRequest.type, {
        textDocument: DOCUMENT,
        position: POSITION,
      }),
    );
    assert.equal(definition.range.start.line, 20);
  }),
);

test(
  "implementation_returns_location",
  withStub(async (stub) => {
    const implementation = location(
      await stub.connection.sendRequest(ImplementationRequest.type, {
        textDocument: DOCUMENT,
        position: POSITION,
      }),
    );
    assert.equal(implementation.range.start.line, 30);
  }),
);

test(
  "document_link_returns_link",
  withStub(async (stub) => {
    const links = await stub.connection.sendRequest(DocumentLinkRequest.type, {
      textDocument: DOCUMENT,
    });
    assert.equal(links?.length, 1);
    assert.equal(links[0].target, "file:///linked");
  }),
);

test(
  "code_lens_returns_lens",
  withStub(async (stub) => {
    const lenses = await stub.connection.sendRequest(CodeLensRequest.type, {
      textDocument: DOCUMENT,
    });
    assert.equal(lenses?.length, 1);
    assert.equal(lenses[0].command?.title, "Run Test");
    assert.equal(lenses[0].command?.command, "test.run");
  }),
);

test(
  "inlay_hint_returns_hint",
  withStub(async (stub) => {
    const hints = await stub.connection.sendRequest(InlayHintRequest.type, {
      textDocument: DOCUMENT,
      range: { start: POSITION, end: { line: 10, character: 0 } },
    });
    assert.equal(hints?.length, 1);
    assert.equal(hints[0].label, ": int");
    assert.equal(hints[0].kind, InlayHintKind.Type);
  }),
);

test(
  "range_formatting_returns_edits",
  withStub(async (stub) => {
    const edits = await stub.connection.sendRequest(
      DocumentRangeFormattingRequest.type,
      {
        textDocument: DOCUMENT,
        range: { start: POSITION, end: { line: 5, character: 0 } },
        options: FORMATTING,
      },
    );
    assert.ok(edits && edits.length >= 1);
    assert.equal(edits[0].newText, "range formatted\n");
  }),
);

test(
  "workspace_symbol_returns_symbols",
  withStub(async (stub) => {
    const symbols = await stub.connection.sendRequest(
      WorkspaceSymbolRequest.type,
      {
        query: "Global",
      },
    );
    assert.ok(symbols && symbols.length >= 1);
    assert.equal(symbols[0].name, "GlobalFunc");
    assert.equal(symbols[0].kind, SymbolKind.Function);
  }),
);
