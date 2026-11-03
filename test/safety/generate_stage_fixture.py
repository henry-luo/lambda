#!/usr/bin/env python3
"""Deterministic public package fixture; no keys, installed apps or host state."""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    target = ROOT / 'test/safety/stage/package.zip'
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, 'w') as archive:
        for name, content, mode in [('data/', b'', 0o40755), ('main.ls', b'"hello"\n', 0o100755),
                                    ('data/message.txt', b'package\n', 0o100644), ('data/empty', b'', 0o100644)]:
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.create_system = 3; info.external_attr = mode << 16
            archive.writestr(info, content)


if __name__ == '__main__':
    main()
