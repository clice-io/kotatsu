"""LSP lifecycle tests: initialize, shutdown, exit."""

import asyncio

import pytest
from lsprotocol.types import (
    ClientCapabilities,
    InitializeParams,
    InitializedParams,
    TextDocumentSyncKind,
)

from conftest import StubClient, _find_stub_server


@pytest.mark.asyncio
async def test_server_capabilities():
    """Server reports expected capabilities."""
    server_path = _find_stub_server()
    c = StubClient()
    await c.start_io(server_path)
    result = await c.initialize_async(
        InitializeParams(capabilities=ClientCapabilities())
    )
    caps = result.capabilities
    assert caps.hover_provider
    assert caps.completion_provider is not None
    assert caps.definition_provider
    assert caps.references_provider
    assert caps.document_symbol_provider
    assert caps.document_formatting_provider
    assert caps.code_action_provider
    assert caps.text_document_sync == TextDocumentSyncKind.Full

    assert result.server_info.name == "stub-server"

    c.initialized(InitializedParams())
    await c.shutdown_async(None)
    c.exit(None)
    await asyncio.sleep(0.1)


@pytest.mark.asyncio
async def test_shutdown_exit():
    """Clean shutdown followed by exit."""
    server_path = _find_stub_server()
    c = StubClient()
    await c.start_io(server_path)
    await c.initialize_async(InitializeParams(capabilities=ClientCapabilities()))
    c.initialized(InitializedParams())
    await c.shutdown_async(None)
    c.exit(None)
    await asyncio.sleep(0.2)


@pytest.mark.asyncio
async def test_exit_without_shutdown():
    """Sending exit without shutdown should still terminate."""
    server_path = _find_stub_server()
    c = StubClient()
    await c.start_io(server_path)
    await c.initialize_async(InitializeParams(capabilities=ClientCapabilities()))
    c.initialized(InitializedParams())
    c.exit(None)
    await asyncio.sleep(0.3)
