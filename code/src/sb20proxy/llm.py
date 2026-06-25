"""Thin OpenAI-compatible client for the POS local models (Ollama on wtrmax.local).

Routine / token-heavy work runs here (capture summaries, drafting, triage) to spare cloud
tokens; nomic-embed-text for embeddings. OpenAI-compatible HTTP, stdlib only (no SDK dep). The
endpoint is a LAN hostname, not a secret. See findings/shared-services-adoption.md.
"""
from __future__ import annotations

import json
import os
import urllib.request

OLLAMA_BASE = os.environ.get("SB20_OLLAMA_BASE", "http://wtrmax.local:11434/v1")
CHAT_MODEL = os.environ.get("SB20_OLLAMA_CHAT_MODEL", "llama3.2:3b")
EMBED_MODEL = os.environ.get("SB20_OLLAMA_EMBED_MODEL", "nomic-embed-text")
_TIMEOUT_S = 120.0


def _post(path: str, body: dict, timeout: float = _TIMEOUT_S) -> dict:
    req = urllib.request.Request(
        OLLAMA_BASE.rstrip("/") + path,
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json", "Authorization": "Bearer ollama"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def chat(
    prompt: str, *, model: str | None = None, system: str | None = None, timeout: float = _TIMEOUT_S
) -> str:
    """One-shot chat completion against the local model; returns the assistant's text."""
    messages = ([{"role": "system", "content": system}] if system else []) + [
        {"role": "user", "content": prompt}
    ]
    data = _post(
        "/chat/completions",
        {"model": model or CHAT_MODEL, "messages": messages, "stream": False},
        timeout,
    )
    return data["choices"][0]["message"]["content"]


def embed(
    texts: str | list[str], *, model: str | None = None, timeout: float = _TIMEOUT_S
) -> list[list[float]]:
    """Embed text(s) with the local embedding model; returns one vector per input."""
    if isinstance(texts, str):
        texts = [texts]
    data = _post("/embeddings", {"model": model or EMBED_MODEL, "input": texts}, timeout)
    return [d["embedding"] for d in data["data"]]
