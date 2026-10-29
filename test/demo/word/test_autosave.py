#!/usr/bin/env python3
"""Exercise Word command-line opening and recovery with an isolated home."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[3]


def event(kind, selector=None, **values):
    return {"type": kind, **({"target": {"selector": selector}} if selector else {}), **values}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lambda-exe", default=str(ROOT / "lambda.exe"))
    parser.add_argument("--real-clock", action="store_true", help="Also wait for the actual two-minute timer")
    args = parser.parse_args()
    executable = str(Path(args.lambda_exe).resolve())
    artifacts = ROOT / "temp/word/tests" / ("autosave-" + str(uuid.uuid4()))
    home = artifacts / "home"
    home.mkdir(parents=True)
    backups = home / ".lambda/word"
    # subprocess-local home isolation keeps fixtures away from the user's recovery copies.
    environment = {**os.environ, "HOME": str(home), "USERPROFILE": str(home)}

    def launch(name, events, options=(), demo=False):
        fixture = artifacts / (name + ".events.json")
        result_path = artifacts / (name + ".result.json")
        fixture.write_text(json.dumps({"name": name, "html": "test/demo/word/word.ls",
                                      "viewport": {"width": 1280, "height": 900}, "events": events}))
        command = ["demo", "word"] if demo else ["view", "test/demo/word/word.ls"]
        result = subprocess.run([executable, "--no-log", *command, "--headless", *options,
                                 "--event-file", str(fixture), "--event-result", str(result_path)],
                                cwd=ROOT, env=environment, capture_output=True, text=True, timeout=180)
        (artifacts / (name + ".stdout")).write_text(result.stdout)
        (artifacts / (name + ".stderr")).write_text(result.stderr)
        report = json.loads(result_path.read_text()) if result_path.exists() else {}
        assert result.returncode == 0 and report.get("result") == "PASS", (name, artifacts, report, result.stderr)
        assert report["assertions"]["passed"] > 0 and report["assertions"]["failed"] == 0, report
        print("PASS " + name, flush=True)

    def tick():
        return event("script", js="document.getElementById('word').dispatchEvent(new Event('word_autosave'))")

    def dirty(value):
        return event("assert_attribute", "#word", attribute="data-dirty", equals=str(value).lower())

    def recover_prompt(count=1):
        return event("assert_count", "[role='dialog'][aria-label='Recover an unsaved document?']", count=count)

    def opened(text):
        return [event("assert_count", "#word-file-picker", count=0, assert_timeout=30000),
                event("assert_text", "#edit-surface", contains=text), dirty(False)]

    text = "Recovery keeps 日本語 and unsaved text."
    launch("versions", [recover_prompt(0), event("click", "#new"), event("type", "#edit-surface", text=text),
                        dirty(True), tick(), event("assert_text", "#status", contains="Recovery snapshot saved"),
                        tick(), dirty(True), event("assert_text", "#edit-surface", contains=text)])
    versions = sorted(backups.glob("*.autosave.word.json"))
    assert len(versions) == 2, versions
    original = {path: path.read_bytes() for path in versions}
    metadata = json.loads(versions[0].read_text())
    document_id = metadata["document_id"]
    corrupt = backups / (document_id + "--9999--bad.autosave.word.json")
    corrupt.write_text("{broken")
    unrelated = backups / ("other--2026--copy.autosave.word.json")
    unrelated.write_text(json.dumps({**metadata, "document_id": "other"}))

    launch("later", [recover_prompt(), event("assert_count", "#word-recovery-version option", count=4),
                     event("assert_script", js="document.getElementById('word-recovery-version').value!==" + json.dumps(str(corrupt))),
                     event("click", "button.edit-dialog-cancel"), recover_prompt(0)])
    assert all(path.read_bytes() == data for path, data in original.items()) and corrupt.exists()
    assert len(list(backups.glob("*.autosave.word.json"))) == 4

    editable = "test/demo/word/tests/fixtures/picker.word.json"
    requested_text = "Opened from the file browser."
    launch("requested-after-later", [recover_prompt(),
        event("assert_text", ".edit-dialog-text", contains="Choose Later to open"),
        event("click", "button.edit-dialog-cancel"), *opened(requested_text)], ["--doc", editable], demo=True)
    assert all(path.read_bytes() == data for path, data in original.items()) and corrupt.exists()
    launch("recover-instead-of-requested", [recover_prompt(),
        event("select_option", "#word-recovery-version", value=str(versions[0])),
        event("click", "button.edit-dialog-recover"), recover_prompt(0),
        event("assert_text", "#edit-surface", contains=text), dirty(True)], ["--doc", editable], demo=True)

    saved_path = artifacts / "recovered.word.json"
    launch("recover-and-save", [recover_prompt(),
        event("select_option", "#word-recovery-version", value=str(corrupt)),
        event("click", "button.edit-dialog-recover"), event("assert_count", "#dialog-error", count=1),
        event("select_option", "#word-recovery-version", value=str(versions[0])),
        event("click", "button.edit-dialog-recover"), recover_prompt(0),
        event("assert_text", "#edit-surface", contains=text), dirty(True),
        event("click", "#save"), event("type", "#word-file-path", clear=True, text=str(saved_path)),
        event("click", "button.edit-dialog-confirm"), dirty(False),
        event("assert_text", "#status", contains="Saved recovered.word.json")])
    assert saved_path.exists() and json.loads(saved_path.read_text())["format"] == "lambda-word"
    assert all(not path.exists() for path in versions) and not corrupt.exists() and unrelated.exists()
    unrelated.unlink()
    launch("clean-restart", [recover_prompt(0), dirty(False)])

    launch("cli-relative", opened(requested_text), ["--doc", editable], demo=True)
    docx = ROOT / "test/input/docx/minimal.docx"
    docx_bytes = docx.read_bytes()
    launch("cli-docx", [*opened("Hello, DOCX!"),
        event("assert_text", "#status", contains="Imported minimal.docx")], ["--doc", str(docx)], demo=True)
    assert docx.read_bytes() == docx_bytes
    spaced = artifacts / "A document with spaces.word.json"
    spaced.write_bytes((ROOT / editable).read_bytes())
    launch("cli-space-equals", opened(requested_text), ["--doc=" + str(spaced)])
    launch("cli-missing-file", [event("assert_count", "#word-picker-error", count=1, assert_timeout=30000),
        event("assert_text", "#edit-surface", contains="Handbook"), dirty(False)],
        ["--doc", str(artifacts / "missing.word.json")], demo=True)
    launch("cli-missing-value", [event("assert_text", "#word-picker-error", contains="--doc requires a document path"),
        event("assert_text", "#edit-surface", contains="Handbook"), dirty(False)], ["--doc"], demo=True)

    if args.real_clock:
        launch("two-minute-clock", [recover_prompt(0), event("click", "#new"),
            event("type", "#edit-surface", text=text), dirty(True),
            event("assert_text", "#status", contains="Recovery snapshot saved", assert_timeout=125000), dirty(True)])
        assert len(list(backups.glob("*.autosave.word.json"))) == 1
    print("Artifacts: " + str(artifacts.relative_to(ROOT)), flush=True)


if __name__ == "__main__":
    main()
