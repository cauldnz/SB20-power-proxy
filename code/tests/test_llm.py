import sb20proxy.llm as llm


def test_chat_parses_assistant_text(monkeypatch):
    captured = {}

    def fake_post(path, body, timeout=llm._TIMEOUT_S):
        captured["path"] = path
        captured["body"] = body
        return {"choices": [{"message": {"role": "assistant", "content": "hello back"}}]}

    monkeypatch.setattr(llm, "_post", fake_post)
    out = llm.chat("hi", system="be terse")
    assert out == "hello back"
    assert captured["path"] == "/chat/completions"
    assert captured["body"]["model"] == llm.CHAT_MODEL
    assert [m["role"] for m in captured["body"]["messages"]] == ["system", "user"]


def test_embed_parses_vectors(monkeypatch):
    def fake_post(path, body, timeout=llm._TIMEOUT_S):
        assert path == "/embeddings"
        return {"data": [{"embedding": [0.1, 0.2, 0.3]}]}

    monkeypatch.setattr(llm, "_post", fake_post)
    assert llm.embed("some text") == [[0.1, 0.2, 0.3]]
