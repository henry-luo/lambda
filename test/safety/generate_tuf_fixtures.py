#!/usr/bin/env python3
"""Generate public TUF fixtures with OpenSSL; private test keys stay in temp/."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
ROLES = ('root', 'timestamp', 'snapshot', 'targets')


def canonical(value):
    # OLPC signing format: unlike JSON, only quote and backslash are escaped.
    if value is None: return 'null'
    if value is True: return 'true'
    if value is False: return 'false'
    if isinstance(value, int): return str(value)
    if isinstance(value, str): return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
    if isinstance(value, list): return '[' + ','.join(map(canonical, value)) + ']'
    if isinstance(value, dict): return '{' + ','.join(canonical(k) + ':' + canonical(value[k]) for k in sorted(value)) + '}'
    raise TypeError(type(value))


def generate(output, payload=None):
    output.mkdir(parents=True, exist_ok=True)
    parent = ROOT / 'temp/app-safety-tuf-fixture-keys'; parent.mkdir(parents=True, exist_ok=True)
    private = Path(tempfile.mkdtemp(prefix='keys-', dir=parent))
    openssl = shutil.which('openssl')
    if not openssl: raise RuntimeError('OpenSSL test tool is missing')
    # The runtime verifier never invokes a process. This tool is only a signing
    # oracle for test data and does not install keys, identities or trust roots.
    env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'TMPDIR': str(ROOT / 'temp/app-safety-build-tmp')}
    def run(*args):
        return subprocess.run([openssl, *map(str, args)], cwd=ROOT, env=env,
                              capture_output=True, check=True, timeout=30).stdout
    def key(label, algorithm='ed25519'):
        filename = private / (label + '.pem')
        native = {'ed25519': 'ED25519', 'rsa': 'RSA', 'ecdsa': 'EC'}[algorithm]
        options = [] if algorithm == 'ed25519' else ['-pkeyopt', 'rsa_keygen_bits:2048'] if algorithm == 'rsa' else ['-pkeyopt', 'ec_paramgen_curve:P-256']
        run('genpkey', '-algorithm', native, *options, '-out', filename)
        der = run('pkey', '-in', filename, '-pubout', '-outform', 'DER')
        public = der[-32:].hex() if algorithm == 'ed25519' else run('pkey', '-in', filename, '-pubout').decode()
        item = {'keytype': algorithm, 'scheme': {'ed25519': 'ed25519', 'rsa': 'rsassa-pss-sha256', 'ecdsa': 'ecdsa-sha2-nistp256'}[algorithm], 'keyval': {'public': public}}
        return {'id': hashlib.sha256(canonical(item).encode()).hexdigest(), 'item': item, 'file': filename, 'algorithm': algorithm}
    sequence = 0
    def sign(payload, signers):
        nonlocal sequence
        sequence += 1; message = private / f'message-{sequence}'; message.write_bytes(canonical(payload).encode())
        signatures = []
        for signer in signers:
            if signer['algorithm'] == 'ed25519':
                signature = run('pkeyutl', '-sign', '-rawin', '-inkey', signer['file'], '-in', message)
            else:
                options = ['-sigopt', 'rsa_padding_mode:pss', '-sigopt', 'rsa_pss_saltlen:32'] if signer['algorithm'] == 'rsa' else []
                signature = run('dgst', '-sha256', '-sign', signer['file'], *options, message)
            signatures.append({'keyid': signer['id'], 'sig': signature.hex()})
        return {'signed': payload, 'signatures': signatures}
    def write(name, data):
        raw = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()
        destination = output / name; destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(raw); return raw
    old = {role: key('old-' + role) for role in ROLES}
    new = {role: key('new-' + role) for role in ROLES}
    def root(keys, revision):
        return {'_type': 'root', 'spec_version': '1.0.37', 'version': revision, 'expires': '2035-01-01T00:00:00Z',
            'consistent_snapshot': True, 'keys': {entry['id']: entry['item'] for entry in keys.values()},
            'roles': {role: {'keyids': [entry['id']], 'threshold': 1} for role, entry in keys.items()}}
    root_raw = write('root.json', sign(root(old, 1), [old['root']]))
    write('2.root.json', sign(root(new, 2), [old['root'], new['root']]))
    write('root-new-only.json', sign(root(new, 2), [new['root']]))
    write('root-skip.json', sign(root(new, 3), [old['root'], new['root']]))
    expired_root = dict(root(old, 1), expires='2020-01-01T00:00:00Z')
    write('root-expired.json', sign(expired_root, [old['root']]))
    threshold_root = root(old, 1)
    threshold_root['roles']['root'] = {'keyids': [old['root']['id'], old['targets']['id']], 'threshold': 2}
    write('root-threshold.json', sign(threshold_root, [old['root'], old['targets']]))
    write('root-threshold-missing.json', sign(threshold_root, [old['root']]))
    alias_root = root(old, 1); alias = '0' * 64
    alias_root['keys'][alias] = alias_root['keys'].pop(old['root']['id'])
    alias_root['roles']['root']['keyids'] = [alias]
    alias_signed = sign(alias_root, [old['root']]); alias_signed['signatures'][0]['keyid'] = alias
    write('root-aliased-key.json', alias_signed)
    if payload is None: payload = b'Lambda authenticated package fixture\x00\xff\n'
    (output / 'artifact.bin').write_bytes(payload)
    description = {'length': len(payload), 'hashes': {'sha256': hashlib.sha256(payload).hexdigest(), 'sha512': hashlib.sha512(payload).hexdigest()},
                   'custom': {'version': '1.2.3', 'unknown_large_integer': 123456789012345678901234567890}}
    targets = {'_type': 'targets', 'spec_version': '1.0.0', 'version': 1, 'expires': '2035-01-01T00:00:00Z', 'targets': {'lambda.pkg': description}}
    targets_raw = write('1.targets.json', sign(targets, [old['targets']]))
    def reference(raw, revision=1):
        return {'version': revision, 'length': len(raw), 'hashes': {'sha256': hashlib.sha256(raw).hexdigest()}}
    snapshot = {'_type': 'snapshot', 'spec_version': '1.0.0', 'version': 1, 'expires': '2035-01-01T00:00:00Z', 'meta': {'targets.json': reference(targets_raw)}}
    snapshot_raw = write('1.snapshot.json', sign(snapshot, [old['snapshot']]))
    timestamp = {'_type': 'timestamp', 'spec_version': '1.0.0', 'version': 1, 'expires': '2035-01-01T00:00:00Z', 'meta': {'snapshot.json': reference(snapshot_raw)}}
    write('timestamp.json', sign(timestamp, [old['timestamp']]))
    for label, field, value in [('timestamp-old', 'version', 1), ('timestamp-new', 'version', 2),
                                ('timestamp-expired', 'expires', '2020-01-01T00:00:00Z')]:
        write(label + '.json', sign(dict(timestamp, **{field: value}), [old['timestamp']]))
    # Authenticated rollback/removal attempts must reach the version checks,
    # rather than being rejected incidentally by a broken signature.
    missing = dict(snapshot, version=2, meta={'other.json': reference(targets_raw)})
    missing_raw = write('snapshot-removed.json', sign(missing, [old['snapshot']]))
    write('timestamp-removed.json', sign(dict(timestamp, version=2, meta={'snapshot.json': reference(missing_raw, 2)}), [old['timestamp']]))
    old_targets = dict(targets, version=2)
    old_targets_raw = write('2.targets.json', sign(old_targets, [old['targets']]))
    old_snapshot = dict(snapshot, version=2, meta={'targets.json': reference(old_targets_raw, 2)})
    old_snapshot_raw = write('2.snapshot.json', sign(old_snapshot, [old['snapshot']]))
    write('timestamp-v2-chain.json', sign(dict(timestamp, version=2, meta={'snapshot.json': reference(old_snapshot_raw, 2)}), [old['timestamp']]))
    def delegation(role, paths=None, terminating=False, prefixes=None):
        item = {'name': role, 'keyids': [old['targets']['id']], 'threshold': 1, 'terminating': terminating}
        item['paths' if paths is not None else 'path_hash_prefixes'] = paths if paths is not None else prefixes
        return item
    for case in ('delegated', 'terminating', 'cycle', 'terminating-cycle', 'hash-prefix', 'wrong-path', 'encoded'):
        first_name = '../名#?role' if case == 'encoded' else 'first'
        first = delegation(first_name, ['*.pkg'], case == 'terminating')
        second = delegation('second', ['*.pkg'])
        if case == 'hash-prefix': first = delegation('first', prefixes=[hashlib.sha256(b'lambda.pkg').hexdigest()[:5]])
        roles = [first] if case in ('hash-prefix', 'wrong-path', 'encoded') else [first, second]
        keyset = {old['targets']['id']: old['targets']['item']}
        top = dict(targets, targets={}, delegations={'keys': keyset, 'roles': roles})
        child = dict(targets, targets={'lambda.pkg': description} if case in ('hash-prefix', 'encoded') else {})
        if case in ('cycle', 'terminating-cycle', 'wrong-path'):
            cycle = case != 'wrong-path'
            child['delegations'] = {'keys': keyset, 'roles': [delegation('first' if cycle else 'second',
                ['*.pkg'] if cycle else ['other/*'], case == 'terminating-cycle')]}
        role_raw = {'targets': write(case + '/1.targets.json', sign(top, [old['targets']])),
                    first_name: write(case + '/1.first.json', sign(child, [old['targets']])),
                    'second': write(case + '/1.second.json', sign(targets, [old['targets']]))}
        # Public URI names and fixture filesystem names are different domains.
        write(case + '/routes.json', {'1.' + quote(role, safe='') + '.json': filename
            for role, filename in [('targets', '1.targets.json'), (first_name, '1.first.json'), ('second', '1.second.json')]})
        snap = dict(snapshot, meta={role + '.json': reference(raw) for role, raw in role_raw.items()})
        snap_raw = write(case + '/1.snapshot.json', sign(snap, [old['snapshot']]))
        write(case + '/timestamp.json', sign(dict(timestamp, meta={'snapshot.json': reference(snap_raw)}), [old['timestamp']]))
    vectors = []
    for algorithm in ('ed25519', 'rsa', 'ecdsa'):
        signer = key('vector-' + algorithm, algorithm)
        signed = sign({'test': 'signature bytes'}, [signer])
        vectors.append({'scheme': signer['item']['scheme'], 'public': signer['item']['keyval']['public'],
                        'message': canonical(signed['signed']), 'signature': signed['signatures'][0]['sig']})
    write('signature-vectors.json', vectors)
    print(json.dumps({'root_sha256': hashlib.sha256(root_raw).hexdigest(), 'public_output': str(output), 'private_test_keys': str(private)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--artifact', type=Path, help='owned test payload; default fixtures are unchanged')
    options = parser.parse_args()
    generate(options.output.resolve(), options.artifact.read_bytes() if options.artifact else None)
