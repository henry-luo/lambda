"""Build the executable JAR fixture with a JDK 17+; run from any directory."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

base = Path(__file__).resolve().parent
root = base.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--java-home', default=os.environ.get('JAVA_HOME'))
args = parser.parse_args()

def java_tool(name):
    executable = name + ('.exe' if os.name == 'nt' else '')
    path = str(Path(args.java_home) / 'bin' / executable) if args.java_home else shutil.which(executable)
    if not path:
        parser.error('A JDK is required; supply --java-home or JAVA_HOME')
    return path

(root / 'temp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='zip-jar-', dir=root / 'temp') as work:
    work = Path(work)
    classes = work / 'classes'
    classes.mkdir()
    subprocess.run([java_tool('javac'), '--release', '8', '-g:none', '-encoding', 'UTF-8',
                    '-d', str(classes), str(base / 'jar-src/example/Hello.java')], check=True)
    resources = {
        'config/settings.json': b'{"answer":42}\n',
        'resources/message.txt': b'Hello from JAR!\n',
        'resources/opaque.bin': bytes((0, 255, 80, 75, 0)),
        'resources/empty.txt': b'',
        'resources/\u00e9 space.txt': 'JAR unicode\n'.encode('utf-8'),
    }
    for name, payload in resources.items():
        path = classes / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    manifest = work / 'MANIFEST.MF'
    manifest.write_bytes(b'Manifest-Version: 1.0\r\nMain-Class: example.Hello\r\n'
                         b'Created-By: Lambda ZIP fixture\r\n\r\n')
    subprocess.run([java_tool('jar'), '--create', '--file', str(base / 'sample.jar'),
                    '--manifest', str(manifest), '--date=2020-01-02T03:04:06Z',
                    '-C', str(classes), '.'], check=True)
