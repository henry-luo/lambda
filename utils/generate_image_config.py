#!/usr/bin/env python3
"""Generate codec headers and dispatch tables from Lambda's build configuration."""
import json
from pathlib import Path
import subprocess
import sys


def write_changed(path, text):
    path = Path(path)
    if path.exists() and path.read_text() == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def main():
    config = json.loads(Path(sys.argv[1]).read_text())['image_codecs']
    for spec in config['generated_files']:
        lines = ['/* generated from build_lambda_config.json; do not edit. */']
        lines += spec.get('preamble', [])
        lines += [f'#define {key} {value}'.rstrip()
                  for key, value in spec.get('defines', {}).items()]
        lines += spec.get('lines', [])
        write_changed(spec['path'], '\n'.join(lines) + '\n')
    for spec in config['dispatch_generators']:
        # Keep the upstream dispatch DSL; feature selection is owned by JSON.
        output = subprocess.check_output(spec['command'], text=True)
        write_changed(spec['path'], output)


if __name__ == '__main__':
    main()
