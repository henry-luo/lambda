#!/usr/bin/env python3
"""Fetch pinned Khronos glTF samples into temp/ and compare their original assets with Three.js.

Run with native graphics access and CHROME_HEADLESS_SHELL set. The --fetch-only
mode prepares assets without rendering; --lambda selects a private executable.
"""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[3]
REVISION = '8f492390ddc7a2ae221a1369ef560589cf8c70fb'
MODELS = ('DamagedHelmet', 'NormalTangentMirrorTest', 'AlphaBlendModeTest', 'TextureSettingsTest')


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'Lambda-Scene3D-qualification'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def prepare(directory):
    tree = json.loads(fetch(f'https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/git/trees/{REVISION}?recursive=1'))
    assert not tree.get('truncated'), 'sample repository tree was truncated'
    records = []
    for entry in tree['tree']:
        path = entry['path']
        if entry['type'] != 'blob' or not any(path.startswith(f'Models/{model}/glTF/') or
                (path.startswith(f'Models/{model}/') and path.count('/') == 2 and path.endswith(('.md', '.txt'))) for model in MODELS):
            continue
        target = directory / path
        content = target.read_bytes() if target.exists() else fetch(
            f'https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/{REVISION}/{path}')
        assert hashlib.sha1(f'blob {len(content)}\0'.encode() + content).hexdigest() == entry['sha'], path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
        records.append({'path': path, 'sha256': hashlib.sha256(content).hexdigest()})
    # Use the same authenticated npm package as the repository's pinned Three.js build.
    vendor = json.loads((ROOT / 'test/demo/scene3d/vendor/three/manifest.json').read_text())
    archive_path = directory / ('three-' + vendor['version'] + '.tgz')
    archive = archive_path.read_bytes() if archive_path.exists() else fetch(vendor['source'])
    assert 'sha512-' + base64.b64encode(hashlib.sha512(archive).digest()).decode() == vendor['integrity']
    archive_path.write_bytes(archive)
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as package:
        for name in ('loaders/GLTFLoader.js', 'utils/BufferGeometryUtils.js', 'utils/SkeletonUtils.js'):
            content = package.extractfile('package/examples/jsm/' + name).read()
            target = directory / 'three' / name
            target.parent.mkdir(parents=True, exist_ok=True);target.write_bytes(content)
            records.append({'path': 'three/' + name, 'sha256': hashlib.sha256(content).hexdigest()})
    for model in MODELS:
        path = directory / f'Models/{model}/glTF/{model}.gltf'
        assert path.exists(), model
        (directory / f'{model}.json').write_text(json.dumps({'width': 512, 'height': 384,
            'model': '/' + str(path.relative_to(ROOT)),
            'loader': '/' + str((directory / 'three/loaders/GLTFLoader.js').relative_to(ROOT))}))
    (directory / 'manifest.json').write_text(json.dumps({'revision': REVISION, 'three': vendor['version'], 'files': records}, indent=2))


def run(directory, executable, minimum):
    subprocess.run([sys.executable, '-c', 'from PIL import Image'], check=True)
    relative = str(directory.relative_to(ROOT))
    subprocess.run(['node', 'test/scene3d/oracle/capture.cjs', relative, '--gltf-samples', *MODELS], cwd=ROOT, check=True)
    results = []
    for model in MODELS:
        description = directory / f'{model}.json'
        # input reads the browser's shared framing; model conversion still starts from the original glTF.
        script = ('import s: lambda.scene3d\nlet d = input(' + json.dumps(str(description.relative_to(ROOT))) + ", 'json')^\n" +
            'let asset = s.load(' + json.dumps(str((directory / f'Models/{model}/glTF/{model}.gltf').relative_to(ROOT))) + ')^;\n' +
            '''<html <head <style "html,body{margin:0;background:#202428}scene3d{display:block}">>
<body (s.scene([s.camera({id: "camera", position: d.position, target: d.target, near: d.near, far: d.far, fov: 45}),
s.light('ambient', {intensity: .2}), s.light('directional', {position: d.lightPosition, target: d.target}), asset],
{camera: "camera", width: d.width, height: d.height, background: "#202428"}))>>\n''')
        source = directory / f'{model}.ls';source.write_text(script)
        events = directory / f'{model}-events.json'
        events.write_text(json.dumps({'name': model, 'viewport': {'width': 512, 'height': 384},
            'events': [{'type': 'wait', 'ms': 200}, {'type': 'render', 'file': f'{relative}/{model}-native.png'}]}))
        native_image = directory / f'{model}-native.png'
        event_result = directory / f'{model}-result.json'
        native_image.unlink(missing_ok=True);event_result.unlink(missing_ok=True)
        with (directory / f'{model}-native.log').open('w') as log:
            native = subprocess.run([str(executable), 'view', str(source.relative_to(ROOT)), '--headless',
                '--event-file', str(events.relative_to(ROOT)), '--event-result', f'{relative}/{model}-result.json'],
                cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        result = {'model': model, 'native_exit': native.returncode}
        result['native_pass'] = native.returncode == 0 and native_image.exists() and event_result.exists()
        if result['native_pass']:
            result['events'] = json.loads(event_result.read_text())
            result['native_pass'] = result['events']['result'] == 'PASS'
        if result['native_pass']:
            manifest = directory / f'{model}-manifest.json'
            manifest.write_text(json.dumps({'canvasRect': {'x': 0, 'y': 0, 'width': 512, 'height': 384}}))
            compared = subprocess.run([sys.executable, 'test/webgl/compare_pixels.py', f'{relative}/{model}-native.png',
                '--reference', f'{relative}/{model}-chromium.png', '--manifest', str(manifest.relative_to(ROOT)),
                '--minimum', str(minimum)], cwd=ROOT, capture_output=True, text=True)
            (directory / f'{model}-compare.json').write_text(compared.stdout)
            (directory / f'{model}-compare.log').write_text(compared.stderr)
            result['compare_exit'] = compared.returncode
            if compared.stdout:
                result['comparison'] = json.loads(compared.stdout)
            else:
                result['error'] = compared.stderr
        results.append(result)
        print(json.dumps(result), flush=True)
    (directory / 'results.json').write_text(json.dumps(results, indent=2))
    return int(any(not result['native_pass'] or result.get('compare_exit', 1) for result in results))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', default='temp/gltf-samples')
    parser.add_argument('--lambda', dest='executable', default='./lambda.exe')
    parser.add_argument('--minimum', type=float, default=.99)
    parser.add_argument('--fetch-only', action='store_true')
    parser.add_argument('--prepared', action='store_true', help='verify and reuse downloaded assets without network access')
    options = parser.parse_args()
    directory = (ROOT / options.directory).resolve()
    assert directory.is_relative_to(ROOT / 'temp'), 'downloaded assets must stay under temp/'
    directory.mkdir(parents=True, exist_ok=True)
    if not .97 <= options.minimum <= 1:
        parser.error('--minimum must stay within the declared PBR acceptance range [0.97,1]')
    if options.prepared:
        manifest = json.loads((directory / 'manifest.json').read_text())
        assert manifest['revision'] == REVISION
        for record in manifest['files']:
            assert hashlib.sha256((directory / record['path']).read_bytes()).hexdigest() == record['sha256'], record['path']
    else:
        prepare(directory)
    if not options.fetch_only:
        raise SystemExit(run(directory, (ROOT / options.executable).resolve(), options.minimum))
