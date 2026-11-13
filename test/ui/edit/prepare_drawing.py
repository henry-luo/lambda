#!/usr/bin/env python3
"""Refresh the isolated SVG used by a drawing-surface UI replay."""
import os
import json
import shutil
import sys

target = sys.argv[1]
host = sys.argv[2]
if not target.startswith("temp/edit-ui/") or ".." in target.split("/"):
    raise ValueError("drawing fixture must be under temp/edit-ui")
if not host.startswith("temp/edit-ui/") or ".." in host.split("/"):
    raise ValueError("drawing host must be under temp/edit-ui")
os.makedirs(os.path.dirname(target), exist_ok=True)
shutil.copyfile("test/ui/edit/sample.svg", target)
with open(host, "w", encoding="utf-8") as script:
    script.write('import host: ~~.~~.test.ui.edit._drawing_host\n')
    script.write('host.page(' + json.dumps(target) + ')^\n')
