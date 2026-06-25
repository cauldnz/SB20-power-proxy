import sb20proxy.obs as obs


def test_build_log_record_shape():
    rec = obs.build_log_record(
        "capture", "ride captured", {"file": "G-x.jsonl", "records": "44000"}
    )
    rl = rec["resourceLogs"][0]
    res_attrs = {a["key"]: a["value"]["stringValue"] for a in rl["resource"]["attributes"]}
    assert res_attrs["service.name"] == obs.SERVICE_NAME
    lr = rl["scopeLogs"][0]["logRecords"][0]
    assert lr["body"]["stringValue"] == "ride captured"
    attrs = {a["key"]: a["value"]["stringValue"] for a in lr["attributes"]}
    assert attrs["event_type"] == "capture"
    assert attrs["plane"] == "personal"
    assert attrs["file"] == "G-x.jsonl"
    assert lr["timeUnixNano"]


def test_emit_never_raises_on_error(monkeypatch):
    monkeypatch.setattr(obs, "_ENABLED", True)

    def boom(*a, **k):
        raise OSError("NAS off / off-LAN")

    monkeypatch.setattr(obs.urllib.request, "urlopen", boom)
    assert obs.emit("health", "x") is False  # swallowed, non-blocking


def test_emit_true_on_2xx(monkeypatch):
    monkeypatch.setattr(obs, "_ENABLED", True)

    class FakeResp:
        status = 204

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

    monkeypatch.setattr(obs.urllib.request, "urlopen", lambda *a, **k: FakeResp())
    assert obs.emit("ota", "flashed", {"result": "ok"}) is True
