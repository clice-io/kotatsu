"""LSP notification tests: didOpen, didChange, publishDiagnostics."""

import asyncio

import pytest
from lsprotocol.types import (
    DiagnosticSeverity,
    DidChangeTextDocumentParams,
    DidCloseTextDocumentParams,
    DidOpenTextDocumentParams,
    DidSaveTextDocumentParams,
    HoverParams,
    Position,
    Range,
    TextDocumentContentChangePartial,
    TextDocumentIdentifier,
    TextDocumentItem,
    VersionedTextDocumentIdentifier,
)

from conftest import StubClient

TEST_URI = "file:///tmp/test.cpp"


@pytest.mark.asyncio
async def test_did_open_diagnostics(client: StubClient):
    """didOpen triggers publishDiagnostics from server."""
    client.text_document_did_open(
        DidOpenTextDocumentParams(
            text_document=TextDocumentItem(
                uri=TEST_URI,
                language_id="cpp",
                version=1,
                text="int main() {}",
            )
        )
    )
    for _ in range(20):
        if TEST_URI in client.diagnostics:
            break
        await asyncio.sleep(0.05)

    assert TEST_URI in client.diagnostics
    diags = client.diagnostics[TEST_URI]
    assert diags[0].message == "stub warning"
    assert diags[0].severity == DiagnosticSeverity.Warning


@pytest.mark.asyncio
async def test_did_change_no_crash(client: StubClient):
    client.text_document_did_change(
        DidChangeTextDocumentParams(
            text_document=VersionedTextDocumentIdentifier(uri=TEST_URI, version=2),
            content_changes=[
                TextDocumentContentChangePartial(
                    range=Range(
                        start=Position(line=0, character=0),
                        end=Position(line=0, character=0),
                    ),
                    text="// changed\n",
                )
            ],
        )
    )
    result = await client.text_document_hover_async(
        HoverParams(
            text_document=TextDocumentIdentifier(uri=TEST_URI),
            position=Position(line=0, character=0),
        )
    )
    assert result is not None


@pytest.mark.asyncio
async def test_did_close_no_crash(client: StubClient):
    client.text_document_did_close(
        DidCloseTextDocumentParams(text_document=TextDocumentIdentifier(uri=TEST_URI))
    )
    result = await client.text_document_hover_async(
        HoverParams(
            text_document=TextDocumentIdentifier(uri=TEST_URI),
            position=Position(line=0, character=0),
        )
    )
    assert result is not None


@pytest.mark.asyncio
async def test_did_save_no_crash(client: StubClient):
    client.text_document_did_save(
        DidSaveTextDocumentParams(text_document=TextDocumentIdentifier(uri=TEST_URI))
    )
    result = await client.text_document_hover_async(
        HoverParams(
            text_document=TextDocumentIdentifier(uri=TEST_URI),
            position=Position(line=0, character=0),
        )
    )
    assert result is not None
