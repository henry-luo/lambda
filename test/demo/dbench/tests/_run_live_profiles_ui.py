#!/usr/bin/env python3
"""Verify profile reconnects against supplied TLS-enabled PostgreSQL and MySQL servers."""
import json
import os
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

from _live_ui_helpers import run_events

root = Path(__file__).resolve().parents[4]
for driver in ("postgresql", "mysql"):
    uri = urlsplit(os.environ["DBENCH_TEST_" + driver.upper() + "_URI"])
    options = parse_qs(uri.query)
    host = unquote(uri.hostname or "")
    if ":" in host:
        host = "[" + host + "]"
    user = unquote(uri.username or "")
    password = unquote(uri.password or "")
    database = unquote(uri.path.lstrip("/"))
    port = uri.port or (5432 if driver == "postgresql" else 3306)
    ca = options.get("sslrootcert" if driver == "postgresql" else "ssl-ca", [""])[0]
    profile = root / ("temp/dbench-live-profile-" + driver + ".json")
    profile.unlink(missing_ok=True)
    events = [
        {"type":"assert_attribute","target":{"selector":"#dbench"},"attribute":"data-connected","equals":"true","assert_timeout":30000},
        {"type":"click","target":{"selector":"#profile-open"}},
        {"type":"type","target":{"selector":"#profile-file"},"text":str(profile.relative_to(root)),"clear":True},
        {"type":"type","target":{"selector":"#profile-name"},"text":"Live profile","clear":True},
        {"type":"select_option","target":{"selector":"#profile-engine"},"value":driver},
        {"type":"type","target":{"selector":"#profile-host"},"text":host,"clear":True},
        {"type":"type","target":{"selector":"#profile-port"},"text":str(port),"clear":True},
        {"type":"type","target":{"selector":"#profile-database"},"text":database,"clear":True},
        {"type":"type","target":{"selector":"#profile-user"},"text":user,"clear":True},
        {"type":"type","target":{"selector":"#profile-ca"},"text":ca,"clear":True},
        {"type":"type","target":{"selector":"#profile-password"},"text":password,"clear":True},
        {"type":"click","target":{"selector":"#profile-save"}},
        {"type":"assert_text","target":{"selector":"#status"},"contains":"Saved profile ","assert_timeout":30000},
        {"type":"click","target":{"selector":"#profile-load"}},
        {"type":"assert_text","target":{"selector":"#status"},"contains":"Loaded profile ","assert_timeout":30000},
        {"type":"assert_script","js":"document.getElementById('profile-password').value===''"},
        {"type":"type","target":{"selector":"#profile-password"},"text":password,"clear":True},
        {"type":"click","target":{"selector":"#profile-connect"}},
        {"type":"assert_attribute","target":{"selector":"#dbench"},"attribute":"data-generation","equals":"2"},
        {"type":"assert_attribute","target":{"selector":"#dbench"},"attribute":"data-connected","equals":"true","assert_timeout":30000},
        {"type":"type","target":{"selector":"#sql"},"text":"SELECT 77 AS answer","clear":True},
        {"type":"click","target":{"selector":"#run"}},
        {"type":"assert_text","target":{"selector":"#result-0-0"},"contains":"77","assert_timeout":30000},
    ]
    try:
        run_events("DBench live profile connection", events)
        text = profile.read_text(encoding="utf-8")
        saved = json.loads(text)
        assert (not password or password not in text) and "password" not in saved
        assert saved["user"] == user and saved["engine"] == driver
    finally:
        profile.unlink(missing_ok=True)
