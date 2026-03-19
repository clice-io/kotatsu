"""Transport robustness tests: large payloads, concurrent requests."""

import asyncio

import pytest
from lsprotocol.types import (
    CompletionParams,
    DidChangeTextDocumentParams,
    DidOpenTextDocumentParams,
    HoverParams,
    Position,
    Range,
    TextDocumentContentChangePartial,
    TextDocumentIdentifier,
    TextDocumentItem,
    VersionedTextDocumentIdentifier,
)

from conftest import StubClient

POS = Position(line=0, character=0)


@pytest.mark.asyncio
async def test_large_uri(client: StubClient):
    """Server handles a request with a moderately large URI."""
    large_uri = "file:///" + "a" * 4_000
    result = await client.text_document_hover_async(
        HoverParams(
            text_document=TextDocumentIdentifier(uri=large_uri),
            position=POS,
        )
    )
    assert result.contents.value == "stub hover"


@pytest.mark.asyncio
async def test_concurrent_requests(client: StubClient):
    """Multiple concurrent requests all get correct responses."""
    tasks = [
        client.text_document_hover_async(
            HoverParams(
                text_document=TextDocumentIdentifier(uri=f"file:///test_{i}.cpp"),
                position=POS,
            )
        )
        for i in range(20)
    ]
    results = await asyncio.gather(*tasks)
    assert len(results) == 20
    assert all(r.contents.value == "stub hover" for r in results)


@pytest.mark.asyncio
async def test_interleaved(client: StubClient):
    """Mix of notifications and requests doesn't break ordering."""
    client.text_document_did_open(
        DidOpenTextDocumentParams(
            text_document=TextDocumentItem(
                uri="file:///interleave.cpp",
                language_id="cpp",
                version=1,
                text="void f() {}",
            )
        )
    )
    hover = await client.text_document_hover_async(
        HoverParams(
            text_document=TextDocumentIdentifier(uri="file:///interleave.cpp"),
            position=POS,
        )
    )
    assert hover is not None

    client.text_document_did_change(
        DidChangeTextDocumentParams(
            text_document=VersionedTextDocumentIdentifier(
                uri="file:///interleave.cpp", version=2
            ),
            content_changes=[
                TextDocumentContentChangePartial(
                    range=Range(start=POS, end=POS),
                    text="// added\n",
                )
            ],
        )
    )
    comp = await client.text_document_completion_async(
        CompletionParams(
            text_document=TextDocumentIdentifier(uri="file:///interleave.cpp"),
            position=POS,
        )
    )
    assert comp is not None

    for _ in range(20):
        if "file:///interleave.cpp" in client.diagnostics:
            break
        await asyncio.sleep(0.05)
    assert "file:///interleave.cpp" in client.diagnostics
