#!/usr/bin/env python3
"""Owned TLS fixture for anonymous updater pulls; never contacts a remote host."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time

from macos_fixture import require_qualification, stage_bundle

ROOT = Path(__file__).resolve().parents[2]
OK, REDIRECT, INVALID, CANCELLED, LIMIT, FORBIDDEN, TLS_ERROR, HTTP_ERROR = range(8)
PAYLOAD = b'{"unverified":"fixture metadata"}'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--verifier', action='store_true', help='also compose the embedded offline verifier and protected host coordinator')
    options = parser.parse_args()
    output = ROOT / 'temp/app_safety_update_pull'
    output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=output))
    report = {'status': 'unqualified', 'cases': [], 'requests': [],
              'remote_connections': False, 'artifact_authentication': False,
              'delivery_available': False, 'default_rollout': False}
    def checkpoint():
        text = json.dumps(report, indent=2) + '\n'
        (directory / 'result.json').write_text(text)
        (output / 'result.json').write_text(text)
    checkpoint()
    environment = dict(os.environ, TMPDIR=str(ROOT / 'temp/app-safety-build-tmp'), PYTHONDONTWRITEBYTECODE='1')
    if not options.no_build:
        with (directory / 'build.log').open('w') as log:
            for command in (['make', 'generate-premake'], ['premake5', 'gmake', '--file=premake5.mac.lua'],
                            ['make', '-C', 'build/premake', 'config=release_native',
                             'test_security_update_pull_gtest', 'lambda-safety-host-fixture', '-j4']):
                subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=900)
            if options.verifier:
                for command in (['make', 'build-update-verifier'],
                                ['make', '-C', 'build/premake', 'config=release_native', 'lambda-safety-bootstrap', '-j4']):
                    subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    pinned = directory / 'bin'; pinned.mkdir()
    binaries = [ROOT / 'test/test_security_update_pull_gtest.exe', ROOT / 'lambda-safety-host-fixture.exe']
    for executable in binaries: shutil.copy2(executable, pinned / executable.name)
    binary, controls = [pinned / executable.name for executable in binaries]
    worker = None
    if options.verifier:
        for name in ['lambda-safety-bootstrap.exe', 'LambdaUpdateRuntime.dylib']:
            shutil.copy2(ROOT / name, pinned / name)
        # Unhardened composition control only. Production still requires an
        # Apple-issued publisher and the separate hardened-load qualification.
        worker = stage_bundle(directory, 'UpdateVerifierControl', pinned / 'lambda-safety-bootstrap.exe', '-', False,
            runtime_image=pinned / 'LambdaUpdateRuntime.dylib', hardened_runtime=False)
    report['artifacts'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in pinned.iterdir() if p.is_file()}
    if options.verifier:
        report['fixture_bundle'] = 'unhardened_ad_hoc_composition_control'
        report['fixture_sha256'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((ROOT / 'test/safety/tuf/fixtures').rglob('*')) if p.is_file()}
    # Ordinary TLS overrides and proxy/credential canaries must not become updater authority.
    canary = directory / 'not-a-CA.pem'; canary.write_text('not a certificate\n')
    netrc = directory / 'netrc'; netrc.write_text('machine updates.example.org login user-secret password private-secret\n')
    environment.update(SSL_CERT_FILE=str(canary), CURL_CA_BUNDLE=str(canary), NETRC=str(netrc),
                       HTTPS_PROXY='http://127.0.0.1:1', ALL_PROXY='http://127.0.0.1:1',
                       https_proxy='http://127.0.0.1:1', all_proxy='http://127.0.0.1:1')
    environment.pop('SSLKEYLOGFILE', None)
    def checked(command, **kwargs):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=20, **kwargs)
        require_qualification(result.returncode == 0, {'command': command, 'exit': result.returncode,
            'stdout': result.stdout.decode(errors='replace'), 'stderr': result.stderr.decode(errors='replace')})
        return result
    checked([str(binary), '--system-roots'], env=environment)
    report['cases'].append({'name': 'system roots ignore previously loaded ordinary CA override', 'passed': True})
    keylog = directory / 'keylog-canary'; keylog.write_bytes(b'unchanged\n')
    checked([str(binary), '--keylog-history'], env=dict(environment, SSLKEYLOGFILE=str(keylog)))
    report['cases'].append({'name': 'TLS key logging history remains ineligible after environment cleared', 'passed': True})
    # Ephemeral fixture TLS certificates: no keychain, publisher or signing identity changes.
    config = directory / 'tls.cnf'
    config.write_text('[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=Lambda updater fixture\n'
                      '[ca]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n'
                      '[server]\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                      'extendedKeyUsage=serverAuth\nsubjectAltName=DNS:updates.example.org,DNS:cdn.example.org\n'
                      '[wrongserver]\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n'
                      'extendedKeyUsage=serverAuth\nsubjectAltName=DNS:other.example.org\n')
    ca, ca_key = directory / 'ca.pem', directory / 'ca.key'
    cert, key, csr = directory / 'server.pem', directory / 'server.key', directory / 'server.csr'
    wrong_cert, other_ca = directory / 'wrong-host.pem', directory / 'other-ca.pem'
    openssl_commands = [
        ['openssl', 'req', '-new', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '1',
         '-config', str(config), '-extensions', 'ca', '-keyout', str(ca_key), '-out', str(ca)],
        ['openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-config', str(config),
         '-keyout', str(key), '-out', str(csr)],
        ['openssl', 'x509', '-req', '-in', str(csr), '-CA', str(ca), '-CAkey', str(ca_key),
         '-CAcreateserial', '-days', '1', '-sha256', '-extfile', str(config), '-extensions', 'server', '-out', str(cert)],
        ['openssl', 'x509', '-req', '-in', str(csr), '-CA', str(ca), '-CAkey', str(ca_key),
         '-CAserial', str(directory / 'ca.srl'), '-days', '1', '-sha256', '-extfile', str(config),
         '-extensions', 'wrongserver', '-out', str(wrong_cert)],
        ['openssl', 'req', '-new', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '1',
         '-config', str(config), '-extensions', 'ca', '-keyout', str(directory / 'other-ca.key'), '-out', str(other_ca)]]
    for command in openssl_commands:
        checked(command, env=environment)
    changed, release = threading.Event(), threading.Event()
    resume_failures = 0
    lock = threading.Lock()
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *_):
            pass
        def do_GET(self):
            nonlocal resume_failures
            with lock:
                report['requests'].append({'method': self.command, 'path': self.path,
                    'headers': list(self.headers.items()), 'body': self.rfile.read(int(self.headers.get('Content-Length', '0'))).hex()})
            name = self.path.rsplit('/', 1)[-1]
            body, status, headers = PAYLOAD, 200, {'Set-Cookie': 'private-server-cookie=secret; Secure'}
            if self.path.startswith('/tuf/'):
                scenario = self.path.split('/')[2]
                filename = self.path.split('/', 3)[3]
                source = ROOT / 'test/safety/tuf/fixtures'
                encoded_files = {}
                if scenario == 'encoded':
                    assets = {p.name: p for p in (source / 'encoded').glob('*.json')}
                    routes = json.loads(assets['routes.json'].read_bytes())
                    encoded_files = {url: assets[filename] for url, filename in routes.items()}
                    encoded_files.update({name: assets[name] for name in ('timestamp.json', '1.snapshot.json')})
                if filename == '2.root.json': status, body = 404, b''
                elif filename in encoded_files: body = encoded_files[filename].read_bytes()
                elif filename == 'timestamp.json': body = (source / ('timestamp-expired.json' if scenario == 'expired' else filename)).read_bytes()
                elif filename in ('1.snapshot.json', '1.targets.json'): body = (source / filename).read_bytes()
                elif filename.endswith('.lambda.pkg'):
                    body = (source / 'artifact.bin').read_bytes()
                    if scenario == 'tamper' or scenario == 'resume' and resume_failures == 0:
                        body = bytes([body[0] ^ 1]) + body[1:]
                        if scenario == 'resume': resume_failures += 1
                else: status, body = 404, b''
                if scenario == 'malformed' and filename == 'timestamp.json': body = b'x' + body[1:]
                if scenario == 'stall' and filename.endswith('.lambda.pkg'):
                    changed.set(); release.wait(10)
            elif name == 'empty.json': body = b''
            elif name == 'exact.json': body = b'x' * 32
            elif name == 'plus-one.json': body = b'x' * 33
            elif name in ('redirect.json', 'relative.json', 'bad-origin.json', 'downgrade.json', 'credentials.json',
                        'query.json', 'dot.json', 'loop.json', 'budget.json'):
                status, body = 302, b'redirect'
                locations = {'redirect.json': f'https://cdn.example.org:{port}/repository/ok.json',
                    'relative.json': 'ok.json', 'bad-origin.json': f'https://other.example.org:{port}/ok.json',
                    'downgrade.json': f'http://updates.example.org:{port}/ok.json',
                    'credentials.json': f'https://user:secret@updates.example.org:{port}/ok.json',
                    'query.json': 'ok.json?user=secret', 'dot.json': '../ok.json', 'loop.json': 'loop.json',
                    'budget.json': 'sixhundred.json'}
                headers['Location'] = locations[name]
                if name == 'budget.json': body = b'x' * 600
            elif name == 'sixhundred.json': body = b'y' * 600
            elif name == 'headers.json': headers['X-Too-Large'] = 'x' * 17000
            elif name == 'encoding.json': headers['Content-Encoding'] = 'gzip'
            elif name == 'not-found.json': status = 404
            elif name == 'auth.json': status, headers = 401, {'WWW-Authenticate': 'Basic realm="fixture"'}
            elif name == 'oversize.json': body = b'x' * 1025
            elif name == 'truncated.json': headers['Content-Length'] = str(len(PAYLOAD) + 100)
            elif name == 'chunked.json': headers['Transfer-Encoding'] = 'chunked'
            elif name == 'stall.json':
                changed.set(); release.wait(10); self.close_connection = True; return
            if 'Transfer-Encoding' not in headers and 'Content-Length' not in headers:
                headers['Content-Length'] = str(len(body))
            if name == 'slow.json': headers['Content-Length'] = '1000'
            self.send_response(status)
            for header, value in headers.items(): self.send_header(header, value)
            self.send_header('Connection', 'close'); self.end_headers()
            try:
                if name == 'slow.json':
                    self.wfile.write(b'initial'); self.wfile.flush(); changed.set(); release.wait(10)
                    self.wfile.write(b'x' * 993)
                elif name == 'chunked.json':
                    for _ in range(11):
                        self.wfile.write(b'64\r\n' + b'x' * 100 + b'\r\n'); self.wfile.flush()
                    self.wfile.write(b'0\r\n\r\n')
                else: self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                pass
            self.close_connection = True
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); tls.load_cert_chain(cert, key)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    serial = 0
    def pull(name, expected, *, path=None, mode='fixture', limit=1024, seconds=5, extra_env=None, ca_file=None, requests=None):
        nonlocal serial
        serial += 1; home = directory / f'home-{serial}'; home.mkdir(mode=0o700)
        command = [str(binary), '--pull-fixture', str(home), str(port), path or name,
                   str(ca_file or ca), mode, str(limit), str(seconds)]
        before = len(report['requests'])
        result = checked(command, env=dict(environment, **(extra_env or {})))
        row = {'name': name, 'result': json.loads(result.stdout), 'http_requests': len(report['requests']) - before}
        report['cases'].append(row); checkpoint()
        require_qualification(row['result']['status'] in expected, row)
        require_qualification(row['result']['policy'] == 'ok', row)
        if requests is not None: require_qualification(row['http_requests'] == requests, row)
        if OK not in expected: require_qualification(row['result']['bytes'] == 0, row)
        return row['result']
    def verify(scenario, expected, *, home=None, mode='plain', budget=1024 * 1024, action=None):
        nonlocal serial
        serial += 1
        if home is None:
            home = directory / f'verifier-home-{serial}'; home.mkdir(mode=0o700)
        first, second = socket.socketpair(); child = host = None
        changed.clear(); release.clear(); control_seconds = None
        try:
            child = subprocess.Popen([str(worker), str(second.fileno()), '29', 'lambda-update-verifier-v1'],
                cwd=directory, env={'PATH': '/usr/bin:/bin', 'LANG': 'en_US.UTF-8'},
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                pass_fds=(second.fileno(),), close_fds=True, start_new_session=True)
            second.close()
            host = subprocess.Popen([str(binary), '--refresh-fixture', str(first.fileno()), '29', str(home),
                str(port), str(ca), scenario, str(budget), mode], cwd=ROOT, env=environment,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, pass_fds=(first.fileno(),), close_fds=True)
            first.close()
            if action:
                require_qualification(changed.wait(5), {'action': action, 'error': 'verifier artifact never reached fixture'})
                start = time.monotonic()
                checked([str(controls), '--fixture-updates', str(home), *action], env=environment)
                control_seconds = time.monotonic() - start
            out, err = host.communicate(timeout=30)
            worker_out, worker_err = child.communicate(timeout=5)
            row = {'name': f'verifier {scenario} {mode}' + (' cancel ' + ' '.join(action) if action else ''),
                'exit': host.returncode, 'worker_exit': child.returncode, 'control_seconds': control_seconds,
                'worker_stdout': worker_out.decode(errors='replace'), 'worker_stderr': worker_err.decode(errors='replace'),
                'host_stderr': err.decode(errors='replace'), 'host_stdout': out.decode(errors='replace')}
            report['cases'].append(row); checkpoint()
            require_qualification(host.returncode == 0, row)
            row['result'] = result = json.loads(out)
            require_qualification(result['ok'] == expected and result['view'] == expected and not worker_out, row)
            if action:
                require_qualification(control_seconds < 3 and result['policy'] != 'ok' and
                    result['transport'] == CANCELLED, row)
            if expected:
                description = json.loads((ROOT / 'test/safety/tuf/fixtures/1.targets.json').read_bytes())['signed']['targets']['lambda.pkg']
                document = json.dumps(description, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()
                require_qualification(child.returncode == 0 and result['description'] and
                    result['description_sha256'] == hashlib.sha256(document).hexdigest() and
                    result['sha256'] == hashlib.sha256((ROOT / 'test/safety/tuf/fixtures/artifact.bin').read_bytes()).hexdigest() and
                    result['after'] == (mode == 'plain'), row)
            else: require_qualification(child.returncode != 0, row)
            return home
        finally:
            release.set()
            first.close(); second.close()
            for process in (host, child):
                if process and process.poll() is None: process.kill(); process.communicate(timeout=5)
    try:
        for _ in range(2):
            result = pull('ok.json', (OK,), requests=1)
            require_qualification(result['sha256'] == hashlib.sha256(PAYLOAD).hexdigest(), result)
        pull('redirect.json', (OK,), requests=2); pull('relative.json', (OK,), requests=2)
        pull('empty.json', (OK,), mode='fixture-zero', requests=1)
        pull('ok.json', (LIMIT,), mode='fixture-zero', requests=1)
        pull('exact.json', (OK,), mode='fixture-exact', requests=1)
        pull('plus-one.json', (LIMIT,), mode='fixture-exact', requests=1)
        for path in ('file%20name.json', 'a%2Fb.json', '..%2Fopaque.json', 'a%252Fb.json', '%E5%90%8D.json', '%25literal.json', 'a%23b%3Fc%40d.json'):
            pull('encoded path data', (OK,), path=path, requests=1)
            require_qualification(report['requests'][-1]['path'] == '/repository/' + path, report['requests'][-1])
        for path in ('%2E/a', '%41', '%2f', '%00', '%0A', '%7F', '%C2%80', '%C0%AF', '%ED%A0%80', '%F4%90%80%80', '%FF', '%', '%GG'):
            pull('invalid encoded path', (INVALID,), path=path, requests=0)
        for path in ('bad-origin.json', 'downgrade.json', 'credentials.json', 'query.json', 'dot.json'):
            pull(path, (INVALID,), requests=1)
        pull('loop.json', (LIMIT,), requests=4)
        for path in ('oversize.json', 'chunked.json', 'headers.json', 'budget.json'):
            pull(path, (LIMIT,), requests=2 if path == 'budget.json' else 1)
        pull('encoding.json', (HTTP_ERROR,), requests=1)
        pull('not-found.json', (HTTP_ERROR,), requests=1); pull('auth.json', (HTTP_ERROR,), requests=1)
        pull('truncated.json', (8,), requests=1)
        pull('ok.json', (FORBIDDEN,), mode='public', requests=0)
        before = len(report['requests'])
        checked([str(binary), '--production-peer', str(port)], env=environment)
        require_qualification(len(report['requests']) == before)
        report['cases'].append({'name': 'production entry refuses loopback with OS roots and no fixture overrides', 'passed': True})
        pull('ok.json', (TLS_ERROR,), ca_file=canary, requests=0)
        pull('untrusted root', (TLS_ERROR,), path='ok.json', ca_file=other_ca, requests=0)
        tls.load_cert_chain(wrong_cert, key)
        try:
            pull('wrong TLS hostname', (TLS_ERROR,), path='ok.json', requests=0)
        finally:
            tls.load_cert_chain(cert, key)
        pull('ok.json', (9,), extra_env={'SSLKEYLOGFILE': str(keylog)}, requests=0)
        require_qualification(keylog.read_bytes() == b'unchanged\n')
        for path in ('../secret', '/etc/passwd', 'timestamp.json?secret', 'https://other.example.org/a', 'a//b'):
            pull('invalid public path', (INVALID,), path=path, requests=0)
        # Cross-process policy edits must complete while a transfer is stalled,
        # then the transfer must abort without handing partial bytes to its caller.
        for action in (('pause', '--all'), ('disable', 'lambda.fixture'), ('offline', 'on')):
            serial += 1; home = directory / f'home-{serial}'; home.mkdir(mode=0o700)
            changed.clear(); release.clear(); process = None
            try:
                process = subprocess.Popen([str(binary), '--pull-fixture', str(home), str(port), 'slow.json',
                    str(ca), 'fixture', '1024', '8'], cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                require_qualification(changed.wait(5), {'action': action, 'error': 'request never reached fixture'})
                start = time.monotonic()
                checked([str(controls), '--fixture-updates', str(home), *action], env=environment)
                elapsed = time.monotonic() - start
                stdout, stderr = process.communicate(timeout=4)
                row = {'name': 'cancel ' + ' '.join(action), 'result': json.loads(stdout),
                       'control_seconds': elapsed, 'exit': process.returncode, 'stderr': stderr.decode(errors='replace')}
                report['cases'].append(row); checkpoint()
                require_qualification(elapsed < 3 and process.returncode == 0 and
                    row['result']['status'] == CANCELLED and row['result']['bytes'] == 0 and
                    row['result']['policy'] != 'ok', row)
            finally:
                release.set()
                if process and process.poll() is None: process.kill(); process.communicate(timeout=5)
        changed.clear(); release.clear(); start = time.monotonic()
        pull('stall.json', (CANCELLED,), seconds=1, requests=1)
        require_qualification(time.monotonic() - start < 4)
        if options.verifier:
            # A same-path persisted timestamp must not suppress retry after a
            # failed target. The next process consumes the actual saved cache.
            reused = verify('valid', True); verify('valid', True, home=reused)
            verify('valid', True, mode='pause-after'); verify('valid', True, mode='new-job')
            for scenario in ('tamper', 'malformed', 'expired'): verify(scenario, False)
            reused = verify('resume', False); verify('resume', True, home=reused)
            verify('valid', False, budget=600)
            reused = verify('encoded', True); verify('encoded', True, home=reused)
            for action in (('pause', '--all'), ('disable', 'lambda.fixture'), ('offline', 'on')):
                verify('stall', False, action=action)
        for request in report['requests']:
            headers = {k.lower(): v for k, v in request['headers']}
            require_qualification(set(headers) <= {'host', 'accept', 'accept-encoding', 'user-agent'}, request)
            require_qualification(headers.get('user-agent') == 'Lambda-Update/1' and
                headers.get('accept-encoding') == 'identity' and request['body'] == '' and
                request['method'] == 'GET', request)
            require_qualification('secret' not in json.dumps(request) and str(home) not in json.dumps(request), request)
        report.update(status='verifier_host_composition_control_passed' if options.verifier else 'anonymous_https_pull_component_passed',
                      passed=len(report['cases']), artifact_authentication=bool(options.verifier),
                      production_publisher_launch_qualified=False)
        checkpoint()
        print(json.dumps({'passed': report['passed'], 'report': str(output / 'result.json'), 'delivery_available': False}))
    finally:
        release.set(); server.shutdown(); server.server_close(); thread.join(timeout=5); checkpoint()


if __name__ == '__main__':
    main()
