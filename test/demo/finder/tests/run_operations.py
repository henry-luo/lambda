"""Replay Finder file operations against an isolated, disposable real directory."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--log", action="store_true", help="Retain native logs in the replay directory.")
args = parser.parse_args()
WORK = ROOT / "temp/finder/operations" / uuid.uuid4().hex
FOLDER = WORK / "files"
(FOLDER / "Source/Subfolder").mkdir(parents=True)
(FOLDER / "Target").mkdir()
(FOLDER / "Existing").mkdir()
(FOLDER / "Source/Subfolder/keep.txt").write_text("nested content survives moves\n")
(FOLDER / "Existing/keep.txt").write_text("delete the folder and its contents\n")
(FOLDER / "alpha.txt").write_text("alpha content ü #\n")
(FOLDER / "second.txt").write_text("second content\n")
(FOLDER / "occupied.txt").write_text("source must survive collision\n")
(FOLDER / "Target/occupied.txt").write_text("destination must survive collision\n")


def event(kind, **fields):
    return {"type": kind, **fields}


def click(selector, **fields):
    return event("click", target={"selector": selector}, **fields)


def check(js):
    return event("assert_script", js=js)


def type_value(value):
    return event("type_physical", target={"selector": "#operation-value"}, text=value, clear=True)


def key(value):
    return event("key_press", key=value)


def rename():
    return event("key_combo", key="e", mods_str="ctrl")


def row(name):
    return '[data-name=' + json.dumps(name, ensure_ascii=False) + ']'


def drag(source, target, **offsets):
    return event("drag_and_drop", target={"selector": source}, to_target={"selector": target, **offsets})


def go(path):
    return [click("#go"), event("type", target={"selector": "#folder-path"}, text=str(path), clear=True), key("enter")]


events = [
    *go(FOLDER),
    click("#new-folder"), type_value("New Folder ü #"), key("enter"),
    check("document.querySelector('[data-name=\"New Folder ü #\"]').getAttribute('data-dir')==='true'"),
    click("#actions"), click("#menu-new-file"), type_value("space # ü.txt"), key("enter"),
    check("document.querySelector('[data-focused=true]').getAttribute('data-name')==='space # ü.txt'"),
    rename(), type_value("renamed.txt"), key("enter"),
    check("document.querySelector('[data-name=\"renamed.txt\"]')!==null && document.querySelector('[data-name=\"space # ü.txt\"]')===null"),
    rename(), type_value("occupied.txt"), key("enter"),
    check("document.getElementById('operation-error').textContent.length>0 && document.getElementById('finder').getAttribute('data-operation')==='rename'"),
    key("escape"), key("delete"), key("escape"),
    check("document.querySelector('[data-name=\"renamed.txt\"]')!==null"),
    key("delete"), click("#operation-confirm"),
    check("document.querySelector('[data-name=\"renamed.txt\"]')===null"),
    click("#new-folder"), type_value("Target"), key("enter"),
    check("document.getElementById('operation-error').textContent.length>0"), key("escape"),
    *([click("#new-folder"), type_value("target"), key("enter"),
       check("document.getElementById('operation-error').textContent.length>0"), key("escape")]
      if (FOLDER / "target").exists() else []),
    event("mouse_down", target={"selector": row("alpha.txt")}),
    event("mouse_move", target={"selector": row("Target")}),
    check("document.getElementById('finder').getAttribute('data-dragging')==='true'"),
    key("escape"), event("mouse_up", target={"selector": row("Target")}),
    check("document.querySelector('[data-name=\"alpha.txt\"]')!==null && document.getElementById('finder').getAttribute('data-dragging')==='false'"),
    drag(row("alpha.txt"), row("Target")),
    check("document.querySelector('[data-name=\"alpha.txt\"]')===null && document.getElementById('error-text').textContent===''"),
    event("dblclick", target={"selector": row("Target")}), click(row("alpha.txt")),
    event("assert_text", target={"selector": "#preview-body pre"}, contains="alpha content ü #"),
    click("#back"), click("#list"),
    click(row("Source")), click("#actions"), click("#menu-move"), type_value(str(FOLDER / "Source/Subfolder")), key("enter"),
    check("document.getElementById('operation-error').textContent.length>0 && document.querySelector('[data-name=\"Source\"]')!==null"), key("escape"),
    drag(row("Source"), row("Target")),
    check("document.querySelector('[data-name=\"Source\"]')===null"),
    event("dblclick", target={"selector": row("Target")}), click(row("Source")),
    click("#actions"), click("#menu-move"), click("#destination-current"),
    check("document.querySelector('#destination-folders [data-destination$=\"/Source\"]').disabled"),
    click("#destination-parent"), click("#operation-confirm"),
    check("document.querySelector('[data-name=\"Source\"]')===null && document.getElementById('error-text').textContent===''"),
    *go(FOLDER), click("#columns"), click(row("Target")),
    click("#column-0 " + row("Source"), button=1), click("#menu-rename"), type_value("Renamed Source"), key("enter"),
    check("document.querySelectorAll('.file-column').length===1 && document.querySelector('[data-name=\"Renamed Source\"]')!==null"),
    drag(row("Renamed Source"), row("Target")),
    check("document.querySelector('[data-name=\"Renamed Source\"]')===null"),
    click(row("Target")), click(row("Renamed Source")), click(row("Subfolder")),
    check("document.querySelectorAll('.file-column').length===4"),
    drag("#column-1 " + row("Renamed Source"), "#column-0 .column-list", offset_x=195, offset_y=390),
    check("document.getElementById('finder').getAttribute('data-path').endsWith('/Renamed Source/Subfolder') && document.querySelectorAll('.file-column').length===2"),
    check("document.querySelector('[data-name=\"keep.txt\"]')!==null"),
    *go(FOLDER), click(row("second.txt")), click(row("occupied.txt"), mods_str="ctrl"),
    check("document.getElementById('finder').getAttribute('data-selected')==='2'"),
    drag(row("second.txt"), row("Target")),
    check("document.querySelector('[data-name=\"second.txt\"]')!==null && document.querySelector('[data-name=\"occupied.txt\"]')!==null && document.getElementById('error-text').textContent.length>0"),
    click(row("second.txt")), key("delete"), click("#operation-confirm"),
    check("document.querySelector('[data-name=\"second.txt\"]')===null"),
    click(row("Existing"), mods_str="ctrl"), click(row("New Folder ü #"), mods_str="ctrl"),
    check("document.getElementById('finder').getAttribute('data-selected')==='2'"),
    key("delete"), click("#operation-confirm"),
    check("document.querySelector('[data-name=\"Existing\"]')===null && document.querySelector('[data-name=\"New Folder ü #\"]')===null"),
    click("#actions"), click("#menu-new-file"), type_value("~"), key("enter"),
    check("document.querySelector('[data-focused=true]').getAttribute('data-name')==='~'"),
    rename(), type_value(".created"), key("enter"),
    check("document.getElementById('finder').getAttribute('data-hidden')==='true' && document.querySelector('[data-focused=true]').getAttribute('data-name')==='.created'"),
    event("capture_surface", source="page", file=str((WORK / "operations.png").relative_to(ROOT))),
    event("window_close"), event("assert_window_closed", closed=True),
]
fixture, result = WORK / "events.json", WORK / "result.json"
fixture.write_text(json.dumps({"name": "Finder real file operations", "viewport": {"width": 1500, "height": 780}, "events": events}, ensure_ascii=False))
executable = os.environ.get("RADIANT_TEST_EXECUTABLE", str(ROOT / "lambda.exe"))
with (WORK / "replay.log").open("w") as log:
    completed = subprocess.run([executable, "view", "test/demo/finder/finder.ls", "--headless", *([] if args.log else ["--no-log"]),
                                "--event-file", str(fixture), "--event-result", str(result)], cwd=ROOT,
                                env=dict(os.environ, LAMBDA_LOG_FILE=str(WORK / "native.log")),
                                stdout=log, stderr=subprocess.STDOUT, timeout=120)
outcome = json.loads(result.read_text()) if result.exists() else {}
if completed.returncode or outcome.get("result") != "PASS" or outcome.get("events") != len(events):
    raise SystemExit(f"Finder operation replay failed: {WORK / 'replay.log'}")
assert {p.name for p in FOLDER.iterdir()} == {"Target", "Renamed Source", "occupied.txt", ".created"}, FOLDER
assert (FOLDER / "Target/alpha.txt").read_text() == "alpha content ü #\n"
assert (FOLDER / "Target/occupied.txt").read_text() == "destination must survive collision\n"
assert (FOLDER / "occupied.txt").read_text() == "source must survive collision\n"
assert (FOLDER / "Renamed Source/Subfolder/keep.txt").read_text() == "nested content survives moves\n"
assert (FOLDER / ".created").read_bytes() == b""
assert not (FOLDER / "Target/second.txt").exists()
print(f"Finder operations: {outcome['assertions']['passed']} UI assertions and filesystem checks passed ({WORK})")
