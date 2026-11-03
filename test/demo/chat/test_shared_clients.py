#!/usr/bin/env python3
"""One Lambda server and two independent native chat windows on loopback."""
import contextlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]


def attribute(name, value):
    return {"type": "assert_attribute", "target": {"selector": "#chat"},
            "attribute": name, "equals": value, "assert_timeout": 15000}


def count(selector, value):
    return {"type": "assert_count", "target": {"selector": selector},
            "count": value, "assert_timeout": 15000}


def send(text):
    return [{"type": "type", "target": {"selector": "#draft"}, "text": text, "clear": True},
            {"type": "click", "target": {"selector": "#send"}}]


def fixture(user, endpoint):
    events = [attribute("data-mode", "client"), attribute("data-user", user),
              attribute("data-online", "true"),
              count("#profile[disabled]", 1), count(".presence.is-online", 1)]
    if endpoint is not None:
        events.insert(2, attribute("data-endpoint", endpoint))
    if user == "alex":
        events += send("Hello Maya from Alex")
    else:
        events += [count('.incoming .bubble[data-status="read"]', 1)]
        events += send("Hi Alex, this is Maya")
    events += [count('.outgoing .bubble[data-status="read"]', 1),
               count('.incoming .bubble[data-status="read"]', 1), attribute("data-messages", "2")]
    return {"name": "Shared server: " + user, "html": "test/demo/chat/chat.ls",
            "viewport": {"width": 1000, "height": 760}, "events": events}


def verify_history(artifacts):
    history = json.loads((artifacts / "history.json").read_text())["messages"]
    assert {(m["from"], m["to"], m["text"], m["status"]) for m in history} == {
        ("alex", "maya", "Hello Maya from Alex", "read"),
        ("maya", "alex", "Hi Alex, this is Maya", "read")}, history


def main():
    executable = str(Path(os.environ.get("LAMBDA_CHAT_EXE", ROOT / "lambda.exe")).resolve())
    artifacts = ROOT / "temp/chat-clients" / uuid.uuid4().hex
    artifacts.mkdir(parents=True)
    children = []
    print("Shared-client artifacts:", artifacts, flush=True)
    with contextlib.ExitStack() as files:
        def launch(name, args, overrides):
            log = files.enter_context((artifacts / (name + ".log")).open("w"))
            env = dict(os.environ, CHAT_DEMO_ROOT=str(artifacts),
                       LAMBDA_LOG_FILE=str(artifacts / (name + "-runtime.log")))
            # inherited launch settings must not change this isolated test's roles.
            for key in ("CHAT_DEMO_ENDPOINT", "CHAT_DEMO_USER", "CHAT_DEMO_PORT"):
                env.pop(key, None)
            env.update(overrides)
            child = subprocess.Popen([executable, *args], cwd=ROOT, env=env, stdout=log, stderr=log)
            children.append(child)
            return child

        try:
            server = launch("server", ["run", "test/demo/chat/server.ls"], {"CHAT_DEMO_PORT": "0"})
            endpoint_file = artifacts / "endpoint.txt"
            deadline = time.monotonic() + 15
            while not endpoint_file.exists() and server.poll() is None and time.monotonic() < deadline:
                time.sleep(.02)
            assert endpoint_file.exists(), "server did not publish its endpoint; see server.log"
            endpoint = endpoint_file.read_text().strip()
            clients = []
            for user in ("alex", "maya"):
                event_file = artifacts / (user + "-events.json")
                event_file.write_text(json.dumps(fixture(user, endpoint)))
                result = artifacts / (user + "-result.json")
                process = launch(user, ["demo", "chat", "--headless", "--event-file", str(event_file),
                                      "--event-result", str(result)],
                                 {"CHAT_DEMO_ENDPOINT": endpoint, "CHAT_DEMO_USER": user})
                clients.append((user, process, result))
            results = {}
            for user, process, result in clients:
                assert process.wait(timeout=90) == 0, user + " window failed; see " + user + ".log"
                results[user] = json.loads(result.read_text())
                assert results[user]["result"] == "PASS", results[user]
            assert server.poll() is None, "closing a client stopped the standalone server"
            verify_history(artifacts)
            (artifacts / "report.json").write_text(json.dumps({"endpoint": endpoint, "clients": results}, indent=2))
            print("PASS: two separate clients exchanged messages and read receipts through one server")
        finally:
            for process in children:
                if process.poll() is None:
                    process.terminate()
            for process in children:
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
