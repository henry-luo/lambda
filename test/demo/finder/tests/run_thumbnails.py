"""Replay real image folders with scrolling, EXIF rotation and unusual filenames."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / "temp/finder/scrolling"
SCRATCH.mkdir(parents=True, exist_ok=True)
for index in range(60):
    shutil.copyfile(ROOT / "test/input/image/sample.png", SCRATCH / f"image-{index:02}.png")
shutil.copyfile(ROOT / "test/input/image/sample.png", SCRATCH / "space # ü.png")
(SCRATCH / "broken.png").write_bytes(b"broken image")
# EXIF orientation 6 rotates the landscape JPEG into a portrait thumbnail.
exif = b"Exif\0\0II" + struct.pack("<HIH", 42, 8, 1)
exif += struct.pack("<HHIHHI", 274, 3, 1, 6, 0, 0)
jpeg = (ROOT / "test/input/image/sample.jpg").read_bytes()
(SCRATCH / "rotated.jpg").write_bytes(jpeg[:2] + b"\xff\xe1" + struct.pack(">H", len(exif) + 2) + exif + jpeg[2:])

def event(kind, **fields):
    return {"type": kind, **fields}

def click(selector):
    return event("click", target={"selector": selector})

def check(js):
    return event("assert_script", js=js)

events = [
    click("#go"), event("type", target={"selector": "#folder-path"}, text=str(SCRATCH), clear=True),
    click("#go-confirm"),
    check("document.querySelector('[data-name=\"broken.png\"] .thumbnail-slot').getAttribute('data-thumb-state')==='error'"),
    check("document.querySelector('[data-name=\"image-00.png\"] .thumbnail-slot').getAttribute('data-thumb-state')==='ready'"),
    check("document.querySelector('[data-name=\"image-59.png\"] .thumbnail-slot').getAttribute('data-thumb-state')==='pending'"),
    # Scroll without changing the model, so this exercises the non-bubbling scroll subscription.
    event("script", js="document.getElementById('files').scrollTop=10000;document.getElementById('files').dispatchEvent(new Event('scroll'))"),
    check("document.querySelector('[data-name=\"image-59.png\"] .thumbnail-slot').getAttribute('data-thumb-state')==='ready'"),
    check("document.querySelector('[data-name=\"image-00.png\"] canvas').width===1"),
    check("document.querySelector('[data-name=\"space # ü.png\"] .thumbnail-slot').getAttribute('data-thumb-state')==='ready'"),
    click("[data-name='rotated.jpg']"),
    check("document.querySelector('#preview-body .thumbnail-slot').getAttribute('data-thumb-state')==='ready'"),
    check("document.querySelector('#preview-body canvas').height>document.querySelector('#preview-body canvas').width"),
    click("#refresh"),
    check("document.querySelector('[data-name=\"image-59.png\"] .thumbnail-slot').getAttribute('data-thumb-state')==='ready'"),
    check("document.querySelector('[data-name=\"image-59.png\"] .thumbnail-slot').getAttribute('data-thumb-cache-hit')==='false'"),
    event("script", js="document.getElementById('files').scrollTop=0;document.getElementById('files').dispatchEvent(new Event('scroll'))"),
    click("#place-demo"),
    check("document.querySelectorAll('#files .thumbnail-slot').length===0 && document.getElementById('finder').getAttribute('data-thumbnail-loading')==='false'"),
    event("window_close"), event("assert_window_closed", closed=True),
]
fixture = SCRATCH / "events.json"
result = SCRATCH / "result.json"
fixture.write_text(json.dumps({"name": "Finder scrolling and image admission", "viewport": {"width": 1000, "height": 520}, "events": events}, ensure_ascii=False))
with (SCRATCH / "replay.log").open("w") as log:
    executable = os.environ.get("RADIANT_TEST_EXECUTABLE", str(ROOT / "lambda.exe"))
    completed = subprocess.run([executable, "view", "test/demo/finder/finder.ls", "--headless", "--event-file", str(fixture), "--event-result", str(result)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
outcome = json.loads(result.read_text()) if result.exists() else {}
if completed.returncode or outcome.get("result") != "PASS":
    raise SystemExit(f"Finder thumbnail replay failed: {SCRATCH / 'replay.log'}")
print(f"Finder scrolling: {outcome['assertions']['passed']} assertions passed")
