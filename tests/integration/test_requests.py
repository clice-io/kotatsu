"""LSP request tests: hover, completion, definition, references, etc."""

import pytest
from lsprotocol.types import (
    CodeAction,
    CodeActionContext,
    CodeActionKind,
    CodeActionParams,
    CodeLensParams,
    CompletionList,
    CompletionParams,
    DeclarationParams,
    DefinitionParams,
    DocumentFormattingParams,
    DocumentHighlightKind,
    DocumentHighlightParams,
    DocumentLinkParams,
    DocumentRangeFormattingParams,
    DocumentSymbol,
    DocumentSymbolParams,
    FoldingRangeParams,
    FormattingOptions,
    HoverParams,
    ImplementationParams,
    InlayHintKind,
    InlayHintParams,
    Location,
    MarkupContent,
    MarkupKind,
    Position,
    PrepareRenameParams,
    Range,
    ReferenceContext,
    ReferenceParams,
    RenameParams,
    SelectionRangeParams,
    SignatureHelpParams,
    SymbolKind,
    TextDocumentIdentifier,
    TypeDefinitionParams,
    WorkspaceEdit,
    WorkspaceSymbolParams,
)

from conftest import StubClient

TEST_URI = "file:///tmp/test.cpp"
POS = Position(line=0, character=0)


def doc(uri: str = TEST_URI) -> TextDocumentIdentifier:
    return TextDocumentIdentifier(uri=uri)


@pytest.mark.asyncio
async def test_hover(client: StubClient):
    result = await client.text_document_hover_async(
        HoverParams(text_document=doc(), position=POS)
    )
    assert isinstance(result.contents, MarkupContent)
    assert result.contents.kind == MarkupKind.Markdown
    assert result.contents.value == "stub hover"


@pytest.mark.asyncio
async def test_completion(client: StubClient):
    result = await client.text_document_completion_async(
        CompletionParams(text_document=doc(), position=POS)
    )
    assert isinstance(result, CompletionList)
    assert not result.is_incomplete
    assert result.items[0].label == "stub_item"


@pytest.mark.asyncio
async def test_definition(client: StubClient):
    result = await client.text_document_definition_async(
        DefinitionParams(text_document=doc(), position=POS)
    )
    loc = result[0] if isinstance(result, list) else result
    assert isinstance(loc, Location)
    assert loc.uri == TEST_URI
    assert loc.range.start.line == 10


@pytest.mark.asyncio
async def test_references(client: StubClient):
    result = await client.text_document_references_async(
        ReferenceParams(
            text_document=doc(),
            position=POS,
            context=ReferenceContext(include_declaration=True),
        )
    )
    assert len(result) == 2
    lines = sorted(loc.range.start.line for loc in result)
    assert lines == [1, 5]


@pytest.mark.asyncio
async def test_document_symbol(client: StubClient):
    result = await client.text_document_document_symbol_async(
        DocumentSymbolParams(text_document=doc())
    )
    assert len(result) >= 1
    sym = result[0]
    assert isinstance(sym, DocumentSymbol)
    assert sym.name == "StubSymbol"
    assert sym.kind == SymbolKind.Function


@pytest.mark.asyncio
async def test_formatting(client: StubClient):
    result = await client.text_document_formatting_async(
        DocumentFormattingParams(
            text_document=doc(),
            options=FormattingOptions(tab_size=4, insert_spaces=True),
        )
    )
    assert len(result) >= 1
    assert result[0].new_text == "formatted\n"


@pytest.mark.asyncio
async def test_code_action(client: StubClient):
    result = await client.text_document_code_action_async(
        CodeActionParams(
            text_document=doc(),
            range=Range(start=POS, end=Position(line=0, character=5)),
            context=CodeActionContext(diagnostics=[]),
        )
    )
    assert len(result) >= 1
    action = result[0]
    assert isinstance(action, CodeAction)
    assert action.title == "stub action"
    assert action.kind == CodeActionKind.QuickFix


@pytest.mark.asyncio
async def test_signature_help(client: StubClient):
    result = await client.text_document_signature_help_async(
        SignatureHelpParams(text_document=doc(), position=POS)
    )
    assert len(result.signatures) == 1
    sig = result.signatures[0]
    assert sig.label == "void foo(int x)"
    assert len(sig.parameters) == 1


@pytest.mark.asyncio
async def test_document_highlight(client: StubClient):
    result = await client.text_document_document_highlight_async(
        DocumentHighlightParams(text_document=doc(), position=POS)
    )
    assert len(result) == 1
    assert result[0].kind == DocumentHighlightKind.Read
    assert result[0].range.start.line == 0


@pytest.mark.asyncio
async def test_rename(client: StubClient):
    result = await client.text_document_rename_async(
        RenameParams(
            text_document=doc(),
            position=POS,
            new_name="new_name",
        )
    )
    assert isinstance(result, WorkspaceEdit)
    assert TEST_URI in result.changes
    edits = result.changes[TEST_URI]
    assert len(edits) == 1
    assert edits[0].new_text == "new_name"


@pytest.mark.asyncio
async def test_prepare_rename(client: StubClient):
    result = await client.text_document_prepare_rename_async(
        PrepareRenameParams(text_document=doc(), position=POS)
    )
    assert result is not None


@pytest.mark.asyncio
async def test_folding_range(client: StubClient):
    result = await client.text_document_folding_range_async(
        FoldingRangeParams(text_document=doc())
    )
    assert len(result) == 1
    assert result[0].start_line == 0
    assert result[0].end_line == 10


@pytest.mark.asyncio
async def test_selection_range(client: StubClient):
    result = await client.text_document_selection_range_async(
        SelectionRangeParams(
            text_document=doc(),
            positions=[POS],
        )
    )
    assert len(result) == 1
    assert result[0].range.start.line == 0


@pytest.mark.asyncio
async def test_declaration(client: StubClient):
    result = await client.text_document_declaration_async(
        DeclarationParams(text_document=doc(), position=POS)
    )
    loc = result[0] if isinstance(result, list) else result
    assert isinstance(loc, Location)
    assert loc.range.start.line == 5


@pytest.mark.asyncio
async def test_type_definition(client: StubClient):
    result = await client.text_document_type_definition_async(
        TypeDefinitionParams(text_document=doc(), position=POS)
    )
    loc = result[0] if isinstance(result, list) else result
    assert isinstance(loc, Location)
    assert loc.range.start.line == 20


@pytest.mark.asyncio
async def test_implementation(client: StubClient):
    result = await client.text_document_implementation_async(
        ImplementationParams(text_document=doc(), position=POS)
    )
    loc = result[0] if isinstance(result, list) else result
    assert isinstance(loc, Location)
    assert loc.range.start.line == 30


@pytest.mark.asyncio
async def test_document_link(client: StubClient):
    result = await client.text_document_document_link_async(
        DocumentLinkParams(text_document=doc())
    )
    assert len(result) == 1
    assert result[0].target == "file:///linked"


@pytest.mark.asyncio
async def test_code_lens(client: StubClient):
    result = await client.text_document_code_lens_async(
        CodeLensParams(text_document=doc())
    )
    assert len(result) == 1
    assert result[0].command.title == "Run Test"
    assert result[0].command.command == "test.run"


@pytest.mark.asyncio
async def test_inlay_hint(client: StubClient):
    result = await client.text_document_inlay_hint_async(
        InlayHintParams(
            text_document=doc(),
            range=Range(start=POS, end=Position(line=10, character=0)),
        )
    )
    assert len(result) == 1
    assert result[0].label == ": int"
    assert result[0].kind == InlayHintKind.Type


@pytest.mark.asyncio
async def test_range_formatting(client: StubClient):
    result = await client.text_document_range_formatting_async(
        DocumentRangeFormattingParams(
            text_document=doc(),
            range=Range(start=POS, end=Position(line=5, character=0)),
            options=FormattingOptions(tab_size=4, insert_spaces=True),
        )
    )
    assert len(result) >= 1
    assert result[0].new_text == "range formatted\n"


@pytest.mark.asyncio
async def test_workspace_symbol(client: StubClient):
    result = await client.workspace_symbol_async(WorkspaceSymbolParams(query="Global"))
    assert len(result) >= 1
    sym = result[0]
    assert sym.name == "GlobalFunc"
    assert sym.kind == SymbolKind.Function
