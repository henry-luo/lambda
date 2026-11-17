#!/usr/bin/env python3
"""Build the isolated MIR L0 experiment using existing release Lambda objects."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'temp/mir_fast_l0')
    args = parser.parse_args()
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    vendor = ROOT / 'lambda/mir'
    copied = directory / 'mir'
    patch = ROOT / 'patches/experiments/mir-fast-l0.patch'
    source_files = [p for p in vendor.rglob('*') if p.suffix in ('.c', '.h')]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in source_files}
    shutil.copytree(vendor, copied, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('*.o', '*.d', '*.a', 'c2m', 'mir-bin-run'))
    with (directory / 'build-experiment.log').open('w') as log:
        subprocess.run(['patch', '-p1', '-i', str(patch)], cwd=copied,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['make', 'libmir.a', '-j8',
                        'GITCOMMIT=99c65079038f3ba9242ef646f308c266cfd7a8e5'],
                       cwd=copied, stdout=log, stderr=subprocess.STDOUT, check=True)
        binary = directory / 'lambda-mir-fast.exe'
        dry = subprocess.check_output([
            'make', '-n', '-f', 'lambda-exe.make', 'config=release_native',
            '-W', '../../lambda/mir/libmir.a', f'TARGET={binary}', 'all'],
            cwd=ROOT / 'build/premake', text=True)
        commands = [shlex.split(line) for line in dry.splitlines() if ' -o ' in line]
        if any('-c' in command for command in commands):
            raise RuntimeError('Release objects are stale; run make build-release-compile first')
        links = [command for command in commands if str(binary) in command]
        if len(links) != 1:
            raise RuntimeError('Cannot identify exactly one generated release link command')
        command = [arg.replace(str(vendor / 'libmir.a'), str(copied / 'libmir.a'))
                   .replace('../../lambda/mir/libmir.a', str(copied / 'libmir.a'))
                   for arg in links[0]]
        (directory / 'link-command.json').write_text(json.dumps(command, indent=2) + '\n')
        subprocess.run(command, cwd=ROOT / 'build/premake',
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    for path, digest in hashes.items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Vendor source changed: {path}')
    metadata = {'binary': str(binary),
                'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
                'vendor_source_hashes': hashes}
    (directory / 'build-provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(f'Built {binary}; vendor sources unchanged')


if __name__ == '__main__':
    main()
