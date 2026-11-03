#!/usr/bin/env python3
"""Official conformance CLI relay; only production Lambda Script decides trust.

This test adapter permits HTTP to an owned numeric loopback server and fixture
files under repository temp/. It is not linked into any Lambda host or worker.
"""
import argparse
from datetime import UTC, datetime
import ipaddress
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib import error, parse, request

ROOT = Path(__file__).resolve().parents[3]
STEP = Path(os.environ.get('LAMBDA_TUF_TEST_STEP', Path(__file__).with_name('_conformance_step.ls')))


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        raise ValueError('conformance transport refuses redirects')


def contained(path):
    resolved = Path(path).resolve()
    resolved.relative_to(ROOT / 'temp')
    return resolved


class Client:
    def __init__(self, options):
        self.options = options
        self.metadata = contained(options.metadata_dir)
        self.directory = Path(tempfile.mkdtemp(prefix='tuf-relay-', dir=ROOT / 'temp'))
        self.environment = dict(os.environ, LAMBDA_HOME=os.environ.get('LAMBDA_TUF_TEST_HOME', str(ROOT / 'lmd')), LAMBDA_EXEC_BACKEND='interp',
                                LAMBDA_NO_LOG='1', LAMBDA_LOG_DIR=str(self.directory))
        self.target = None

    def close(self):
        shutil.rmtree(self.directory)

    def step(self, op, **values):
        (self.directory / 'request.json').write_text(json.dumps(dict(op=op, **values)))
        reply = self.directory / 'reply.json'; reply.unlink(missing_ok=True)
        result = subprocess.run([os.environ.get('LAMBDA_TUF_TEST_EXECUTABLE', str(ROOT / 'lambda.exe')),
            '--no-log', 'run', str(STEP)], cwd=self.directory, env=self.environment,
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        if result.returncode or not reply.exists():
            raise RuntimeError(result.stderr.decode(errors='replace'))
        response = json.loads(reply.read_bytes())
        if not response['ok']: raise ValueError('Lambda verifier rejected the transition')
        return response

    def fetch(self, action):
        base = self.options.target_base_url if action['artifact'] else self.options.metadata_url
        address = parse.urlsplit(base)
        if address.scheme != 'http' or not ipaddress.ip_address(address.hostname).is_loopback or address.username or address.password:
            raise ValueError('conformance transport requires owned numeric loopback HTTP')
        relative = action['path']
        url = base.rstrip('/') + '/' + relative
        # Explicit empty proxy map excludes ambient proxy/credential profiles.
        opener = request.build_opener(request.ProxyHandler({}), NoRedirect())
        try:
            with opener.open(url, timeout=10) as stream:
                body = stream.read(action['limit'] + 1)
                if len(body) > action['limit']: raise ValueError('signed response limit exceeded')
                return 'ok', body
        except error.HTTPError as failure:
            if failure.code not in (404, 410): raise
            return 'not_found', b''

    def drive(self, action):
        for _ in range(256):
            if action['kind'] == 'done': return action
            if action['kind'] == 'checkpoint':
                rows = action['entries']; names = {parse.quote(row['role'], safe='') + '.json' for row in rows}
                for filename in self.metadata.rglob('*.json'):
                    if str(filename.relative_to(self.metadata)) not in names: filename.unlink()
                for row in rows:
                    filename = self.metadata / (parse.quote(row['role'], safe='') + '.json')
                    filename.resolve().relative_to(self.metadata)
                    filename.parent.mkdir(parents=True, exist_ok=True)
                    filename.write_bytes(bytes.fromhex(row['hex']))
                (self.directory / 'response.bin').write_bytes(b'')
                action = self.step('advance', status='ok')
            elif action['kind'] == 'fetch':
                status, body = None, None
                if action['artifact']: self.target_from_cache = False
                if action['artifact'] and self.target.exists():
                    (self.directory / 'response.bin').write_bytes(self.target.read_bytes())
                    if self.step('cached_target')['valid']:
                        status, body = 'ok', self.target.read_bytes(); self.target_from_cache = True
                if status is None: status, body = self.fetch(action)
                (self.directory / 'response.bin').write_bytes(body)
                action = self.step('advance', status=status)
            else: raise ValueError('unexpected verifier action')
        raise ValueError('action budget exhausted')

    def run(self):
        cache = [{'role': parse.unquote(str(p.relative_to(self.metadata))[:-5], errors='strict'), 'filename': str(p)}
                 for p in sorted(self.metadata.rglob('*.json'))]
        self.drive(self.step('begin', cache=cache, utc=datetime.now(UTC).strftime('%Y-%m-%dT%H:%M:%SZ')))
        if self.options.command == 'refresh': return
        destination = contained(self.options.target_dir)
        for name in self.options.target_name:
            self.target = destination / ('target-' + hashlib.sha256(name.encode('utf8')).hexdigest())
            self.target.resolve().relative_to(destination)
            self.drive(self.step('select', target=name))
            if not self.target_from_cache:
                self.target.parent.mkdir(parents=True, exist_ok=True)
                self.target.write_bytes((self.directory / 'response.bin').read_bytes())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--metadata-dir', required=True)
    parser.add_argument('--metadata-url')
    parser.add_argument('--target-name', action='append')
    parser.add_argument('--target-base-url')
    parser.add_argument('--target-dir')
    parser.add_argument('command', choices=('init', 'refresh', 'download'))
    parser.add_argument('root', nargs='?')
    options = parser.parse_args()
    client = None
    try:
        if options.command == 'init':
            contained(options.metadata_dir).joinpath('root.json').write_bytes(contained(options.root).read_bytes())
        else:
            client = Client(options); client.run()
        return 0
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as failure:
        sys.stderr.write('Lambda conformance client: ' + str(failure) + '\n'); return 1
    finally:
        if client: client.close()


if __name__ == '__main__':
    sys.exit(main())
