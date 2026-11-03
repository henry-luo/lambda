#!/usr/bin/env python3
"""Actual offline Lambda decoder + protected native host staging mechanism."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading

from macos_fixture import require_qualification, stage_bundle
from test_package_stage import zip_bytes, single

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    base = ROOT / 'temp/app_safety_update_stage'; base.mkdir(exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    environment = dict(os.environ, TMPDIR=str(ROOT / 'temp/app-safety-build-tmp'), PYTHONDONTWRITEBYTECODE='1')
    if not args.no_build:
        with (directory / 'build.log').open('w') as log:
            for command in (['make', 'generate-premake'], ['premake5', 'gmake', '--file=premake5.mac.lua'],
                            ['make', '-C', 'build/premake', 'config=release_native', 'test_security_update_stage_gtest', 'test_security_update_pull_gtest', 'lambda-safety-bootstrap', '-j4'],
                            ['make', 'build-update-verifier']):
                subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
    pinned = directory / 'bin'; pinned.mkdir()
    for filename in ['lambda-safety-bootstrap.exe', 'LambdaUpdateRuntime.dylib', 'test/test_security_update_stage_gtest.exe', 'test/test_security_update_pull_gtest.exe']:
        shutil.copy2(ROOT / filename, pinned / Path(filename).name)
    host = pinned / 'test_security_update_stage_gtest.exe'
    worker = stage_bundle(directory, 'ProtectedStageControl', pinned / 'lambda-safety-bootstrap.exe', '-', False,
        runtime_image=pinned / 'LambdaUpdateRuntime.dylib', hardened_runtime=False)
    report = {'status': 'unqualified', 'cases': [], 'delivery_available': False, 'default_rollout': False,
              'fixture_bundle': 'unhardened_ad_hoc_composition_control', 'signature_verified_cases': 0,
              'mechanism_artifact_source': 'test-only opaque artifact factory; production accepts verifier artifacts only',
              'artifacts': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in pinned.iterdir()},
              'launched_fixture_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                  (worker, worker.parent.parent / 'Frameworks/LambdaSafetyRuntime.dylib')}}
    def checkpoint():
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        (base / 'result.json').write_text((directory / 'result.json').read_text())
    def manage(home, flag, *extra):
        result = subprocess.run([str(host), flag, str(home), *extra], cwd=ROOT, env=environment,
                                capture_output=True, timeout=15)
        require_qualification(result.returncode == 0, result.stderr.decode(errors='replace'))
        return json.loads(result.stdout)
    def receipt(image, expected):
        raw=(image.parent/'manifest.bin').read_bytes(); offset=0; found={}
        while offset<len(raw):
            flags,size=struct.unpack_from('<IQ',raw,offset); offset+=12
            end=raw.index(b'\0',offset); path=raw[offset:end].decode('ascii'); offset=end+1
            digest=raw[offset:offset+32]; offset+=32
            require_qualification(flags & ~3 == 0 and path not in found and path in expected,path)
            directory,content=expected[path]
            require_qualification(bool(flags & 1)==directory and size==len(content) and digest==hashlib.sha256(content).digest(),path)
            found[path]=flags
        require_qualification(offset==len(raw) and set(found)==set(expected))
        return found
    def check(label, data, expected=None, mode='normal'):
        index = len(report['cases']); home = directory / f'home-{index}'; home.mkdir(mode=0o700)
        source = directory / f'archive-{index}.zip'; source.write_bytes(data)
        canary = home / 'project.txt'; canary.write_bytes(b'user file\n')
        private = home / '.lambda/app-data'; private.mkdir(parents=True, mode=0o700)
        (home / '.lambda').chmod(0o700); retained = private / 'retained'; retained.write_bytes(b'retain app data\n')
        first, second = socket.socketpair(); child = parent = None
        try:
            child = subprocess.Popen([str(worker), str(second.fileno()), '29', 'lambda-package-stage-v1'],
                cwd=directory, env={'PATH': '/usr/bin:/bin', 'LANG': 'en_US.UTF-8'}, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, pass_fds=(second.fileno(),), close_fds=True)
            second.close()
            parent = subprocess.Popen([str(host), '--stage-fixture', str(first.fileno()), str(home), str(source), mode],
                cwd=ROOT, env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                pass_fds=(first.fileno(),), close_fds=True)
            first.close(); out, err = parent.communicate(timeout=30); child_out, child_err = child.communicate(timeout=5)
            (directory / f'host-{index}.log').write_bytes(out + err)
            (directory / f'worker-{index}.log').write_bytes(child_out + child_err)
            require_qualification(parent.returncode == 0, {'case': label, 'stderr': err.decode(errors='replace')})
            result = json.loads(out)
            require_qualification(result['ok'] == (expected is not None), result)
            require_qualification(child.returncode == (0 if expected is not None else 3), {'case': label, 'exit': child.returncode})
            if expected is not None:
                require_qualification(result['ready'] and result['view'] == (mode != 'pause'), result)
                image = home / '.lambda/package-staging' / ('s-' + result['id']) / 'image'
                actual = {str(p.relative_to(image)): (p.is_dir(), b'' if p.is_dir() else p.read_bytes()) for p in image.rglob('*')}
                require_qualification(actual == expected, {'case': label, 'paths': list(actual)})
                flags=receipt(image,expected)
                if 'main.ls' in flags: require_qualification(flags['main.ls']==2)
                for p in image.rglob('*'):
                    require_qualification(not p.is_symlink() and (p.is_dir() or p.stat().st_nlink == 1))
                    require_qualification(p.stat().st_mode & 0o777 == (0o700 if p.is_dir() else 0o600), str(p))
            inspect = manage(home, '--inspect-stage'); require_qualification(inspect['ok'], inspect)
            require_qualification(inspect['ready'] == (expected is not None), inspect)
            wrong = ('0' if inspect['id'][0] != '0' else '1') + inspect['id'][1:]
            require_qualification(manage(home, '--discard-stage', wrong, '1')['result'] == 'conflict')
            complete = False; steps = 0
            while not complete and steps < 30:
                cleared = manage(home, '--discard-stage', inspect['id'], '1'); require_qualification(cleared['ok'], cleared)
                complete = cleared['complete']; steps += 1
            require_qualification(complete and manage(home, '--inspect-stage')['result'] == 'missing')
            require_qualification(canary.read_bytes() == b'user file\n' and retained.read_bytes() == b'retain app data\n')
            report['cases'].append({'name': label, 'passed': True, 'result': result, 'cleanup_steps': steps,
                                    'archive_sha256': hashlib.sha256(data).hexdigest()}); checkpoint()
        finally:
            first.close(); second.close()
            for process in (parent, child):
                if process and process.poll() is None: process.kill(); process.communicate(timeout=5)
    checkpoint()
    sample = (ROOT / 'test/safety/stage/package.zip').read_bytes()
    expected = {'data': (True, b''), 'main.ls': (False, b'"hello"\n'), 'data/message.txt': (False, b'package\n'), 'data/empty': (False, b'')}
    check('actual decoder protected file image and retained user data', sample, expected)
    check('pause invalidates staged view but permits cleanup', sample, expected, 'pause')
    check('nested implicit directories and literal percent', single('a/b/100% ready.txt', b'\x00\xff'),
          {'a': (True,b''), 'a/b': (True,b''), 'a/b/100% ready.txt': (False,b'\x00\xff')})
    check('empty image receipt', zip_bytes([]), {})
    for label, data in [('traversal', single('../project.txt')), ('absolute', single('/outside')),
                        ('symlink', single('link', b'../project.txt', 0o120777)),
                        ('case collision', zip_bytes([('A', b'a', 0o100644), ('a', b'b', 0o100644)])),
                        ('privileged mode', single('tool', b'x', 0o104755)), ('truncated', sample[:-5])]:
        check(label, data)
    signed_repository = directory / 'signed-repository'
    bad_repository = directory / 'signed-bad-repository'
    bad_payload = directory / 'signed-traversal.zip'; bad_payload.write_bytes(single('../project.txt'))
    for destination, payload in [(signed_repository, ROOT / 'test/safety/stage/package.zip'), (bad_repository, bad_payload)]:
        result = subprocess.run(['python3', str(ROOT / 'test/safety/generate_tuf_fixtures.py'), '--output', str(destination), '--artifact', str(payload)],
            cwd=ROOT, env=environment, capture_output=True, timeout=90)
        require_qualification(result.returncode == 0, result.stderr.decode(errors='replace'))
        (destination / 'generation.json').write_bytes(result.stdout)
    cert, key, config = directory / 'server.pem', directory / 'server.key', directory / 'tls.cnf'
    config.write_text('[req]\ndistinguished_name=dn\nprompt=no\nx509_extensions=server\n[dn]\nCN=updates.example.org\n'
                      '[server]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\n'
                      'extendedKeyUsage=serverAuth\nsubjectAltName=DNS:updates.example.org\n')
    result = subprocess.run(['openssl','req','-new','-x509','-newkey','rsa:2048','-nodes','-sha256','-days','1',
                             '-config',str(config),'-keyout',str(key),'-out',str(cert)], cwd=ROOT, env=environment,
                            capture_output=True, timeout=30)
    require_qualification(result.returncode == 0, result.stderr.decode(errors='replace'))
    requests = []
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            requests.append({'path':self.path,'headers':list(self.headers.items())})
            scenario, filename = self.path.split('/')[2], self.path.split('/',3)[3]
            repository = bad_repository if scenario == 'bad-archive' else signed_repository
            status, body = 200, b''
            if filename in ('timestamp.json','1.snapshot.json','1.targets.json'): body = (repository/filename).read_bytes()
            elif filename.endswith('.lambda.pkg'):
                body = (repository/'artifact.bin').read_bytes()
                if scenario == 'tamper': body = bytes([body[0]^1])+body[1:]
            else: status = 404
            self.send_response(status); self.send_header('Content-Length',str(len(body))); self.end_headers()
            try: self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError,ssl.SSLError): pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler); server.daemon_threads=True
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(cert,key)
    server.socket=context.wrap_socket(server.socket,server_side=True)
    thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
    try:
        for scenario in ['valid','tamper','bad-archive']:
            home=directory/('signed-home-'+scenario); home.mkdir(mode=0o700)
            canary=home/'project.txt'; canary.write_bytes(b'preserved\n')
            a,b=socket.socketpair(); c,d=socket.socketpair(); processes=[]
            try:
                for channel,entry in [(b,'lambda-update-verifier-v1'),(d,'lambda-package-stage-v1')]:
                    process=subprocess.Popen([str(worker),str(channel.fileno()),'29',entry],cwd=directory,
                        env={'PATH':'/usr/bin:/bin','LANG':'en_US.UTF-8'},stdin=subprocess.DEVNULL,
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE,pass_fds=(channel.fileno(),),close_fds=True)
                    processes.append(process); channel.close()
                repository=bad_repository if scenario=='bad-archive' else signed_repository
                command=[str(pinned/'test_security_update_pull_gtest.exe'),'--refresh-stage',str(a.fileno()),'29',str(home),
                    str(server.server_address[1]),str(cert),scenario,str(1024*1024),'plain',str(c.fileno()),str(repository/'root.json')]
                parent=subprocess.Popen(command,cwd=ROOT,env=environment,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,pass_fds=(a.fileno(),c.fileno()),close_fds=True)
                processes.append(parent); a.close(); c.close(); out,err=parent.communicate(timeout=30)
                require_qualification(parent.returncode==0,err.decode(errors='replace')); row=json.loads(out)
                for process in processes[:2]:
                    child_out,child_err=process.communicate(timeout=5)
                    (directory/(scenario+'-'+str(process.pid)+'.log')).write_bytes(child_out+child_err)
                require_qualification(row['ok']==(scenario!='tamper') and row['staged']==(scenario=='valid'),row)
                require_qualification(processes[0].returncode==(3 if scenario=='tamper' else 0) and
                    processes[1].returncode==(0 if scenario=='valid' else 3),row)
                if scenario=='valid':
                    image=home/'.lambda/package-staging'/('s-'+row['stage_id'])/'image'
                    require_qualification(row['stage_ready'] and row['stage_entries']==4 and row['stage_bytes']==16,row)
                    require_qualification((image/'data/message.txt').read_bytes()==b'package\n')
                    receipt(image,expected)
                    protected=json.loads((image.parent/'description.json').read_bytes())
                    require_qualification(protected['custom']['unknown_large_integer']==123456789012345678901234567890)
                inspected=manage(home,'--inspect-stage')
                if scenario=='tamper': require_qualification(inspected['result']=='missing',inspected)
                else:
                    require_qualification(inspected['ok'] and inspected['ready']==(scenario=='valid'),inspected)
                    for _ in range(20):
                        cleared=manage(home,'--discard-stage',inspected['id'],'1'); require_qualification(cleared['ok'],cleared)
                        if cleared['complete']: break
                    require_qualification(cleared['complete'])
                require_qualification(canary.read_bytes()==b'preserved\n')
                report['cases'].append({'name':'signed HTTPS -> Lambda verifier -> staging: '+scenario,'passed':True,'result':row})
                checkpoint()
            finally:
                for channel in [a,b,c,d]: channel.close()
                for process in processes:
                    if process.poll() is None: process.kill(); process.communicate(timeout=5)
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)
    for request in requests:
        headers={name.lower():value for name,value in request['headers']}
        require_qualification(not any(name in headers for name in ['cookie','authorization','proxy-authorization','content-length']),request)
        require_qualification('?' not in request['path'] and '#' not in request['path'],request)
    report['signed_pipeline'] = True; report['signature_verified_cases'] = 3; report['requests']=requests
    report['status'] = 'verified_artifact_protected_staging_component_passed_activation_and_deployment_unqualified'
    checkpoint(); print(json.dumps({'passed':len(report['cases']),'report':str(base/'result.json')}))


if __name__ == '__main__':
    main()
