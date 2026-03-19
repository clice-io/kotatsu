"""LSP error handling tests."""

import pytest
from lsprotocol.types import (
    HoverParams,
    Position,
    SemanticTokensParams,
    TextDocumentIdentifier,
)

from conftest import StubClient


@pytest.mark.asyncio
async def test_hover_error_response(client: StubClient):
    """Hover with magic error URI returns ResponseError."""
    with pytest.raises(Exception) as exc_info:
        await client.text_document_hover_async(
            HoverParams(
                text_document=TextDocumentIdentifier(uri="file:///error"),
                position=Position(line=0, character=0),
            )
        )
    assert "hover error triggered" in str(exc_info.value)


@pytest.mark.asyncio
async def test_method_not_found(client: StubClient):
    """Unregistered method returns error."""
    with pytest.raises(Exception):
        await client.text_document_semantic_tokens_full_async(
            SemanticTokensParams(
                text_document=TextDocumentIdentifier(uri="file:///test.cpp")
            )
        )
