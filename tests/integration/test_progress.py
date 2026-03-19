"""LSP progress tests: $/progress begin, report, end."""

import asyncio

import pytest
from lsprotocol.types import (
    CompletionParams,
    Position,
    TextDocumentIdentifier,
)

from conftest import StubClient


@pytest.mark.asyncio
async def test_work_done_progress(client: StubClient):
    """Completion with magic URI triggers progress sequence."""
    result = await client.text_document_completion_async(
        CompletionParams(
            text_document=TextDocumentIdentifier(uri="file:///progress"),
            position=Position(line=0, character=0),
        )
    )
    assert result is not None

    for _ in range(40):
        if len(client.progress_events) >= 3:
            break
        await asyncio.sleep(0.05)

    assert "test-progress" in client.progress_tokens
    assert len(client.progress_events) >= 3
    kinds = [
        e["value"]["kind"]
        for e in client.progress_events
        if isinstance(e["value"], dict)
    ]
    assert "begin" in kinds
    assert "report" in kinds
    assert "end" in kinds
