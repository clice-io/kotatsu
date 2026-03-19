"""Fixtures for LSP integration tests using pygls LanguageClient."""

import asyncio
import os
from pathlib import Path

import pytest
import pytest_asyncio
from lsprotocol.types import (
    PROGRESS,
    TEXT_DOCUMENT_PUBLISH_DIAGNOSTICS,
    WINDOW_WORK_DONE_PROGRESS_CREATE,
    ClientCapabilities,
    Diagnostic,
    InitializeParams,
    InitializedParams,
    ProgressParams,
    PublishDiagnosticsParams,
    WorkDoneProgressCreateParams,
)
from pygls.lsp.client import BaseLanguageClient


def _find_stub_server() -> str:
    """Locate the lsp_stub_server binary."""
    env = os.environ.get("LSP_STUB_SERVER")
    if env:
        return env

    root = Path(__file__).parent.parent.parent
    candidates = [
        root / "build" / "tests" / "integration" / "lsp_stub_server",
        root / "build" / "lsp_stub_server",
    ]
    for p in candidates:
        if p.exists():
            return str(p)
        if p.with_suffix(".exe").exists():
            return str(p.with_suffix(".exe"))

    pytest.skip("lsp_stub_server not found; build it first")


class StubClient(BaseLanguageClient):
    """Language client that tracks server-sent notifications."""

    def __init__(self):
        super().__init__("stub-test-client", "0.1.0")
        self.diagnostics: dict[str, list[Diagnostic]] = {}
        self.progress_tokens: list[str] = []
        self.progress_events: list[dict] = []

        @self.feature(TEXT_DOCUMENT_PUBLISH_DIAGNOSTICS)
        def on_diagnostics(params: PublishDiagnosticsParams):
            self.diagnostics[params.uri] = list(params.diagnostics)

        @self.feature(WINDOW_WORK_DONE_PROGRESS_CREATE)
        def on_create_progress(params: WorkDoneProgressCreateParams):
            token = str(params.token) if isinstance(params.token, int) else params.token
            self.progress_tokens.append(token)
            return None

        @self.feature(PROGRESS)
        def on_progress(params: ProgressParams):
            token = str(params.token) if isinstance(params.token, int) else params.token
            self.progress_events.append({"token": token, "value": params.value})


@pytest_asyncio.fixture
async def client():
    """Spawn stub server, initialize, yield client, then shutdown+exit."""
    server_path = _find_stub_server()
    c = StubClient()
    await c.start_io(server_path)

    result = await c.initialize_async(
        InitializeParams(capabilities=ClientCapabilities())
    )
    assert result is not None
    c.initialized(InitializedParams())

    yield c

    await c.shutdown_async(None)
    c.exit(None)
    await asyncio.sleep(0.1)
