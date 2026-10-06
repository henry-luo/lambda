#!/usr/bin/env python3
"""Build the JSON-owned browser profile with Emscripten, never native archives."""

import argparse
import ast
import concurrent.futures
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run_logged(command, log, env, cwd=ROOT):
    with log.open('w') as output:
        result = subprocess.run(command, cwd=cwd, env=env,
                                stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); see {log}")


def compiler_path():
    explicit = os.environ.get('EMCC')
    if explicit:
        return Path(explicit).resolve()
    installed = shutil.which('emcc')
    if installed:
        return Path(installed).resolve()
    sdk = Path(os.environ.get('EMSDK', ROOT / 'temp/wasm-toolchain/emsdk'))
    return sdk / 'upstream/emscripten/emcc'


def compiler_environment(tools, build):
    env = dict(os.environ, TMPDIR=str(build))
    env['PATH'] = str(tools) + os.pathsep + env['PATH']
    # use the activated SDK's Python when the caller has not sourced emsdk_env.
    config = Path(env.get('EM_CONFIG', tools.parent.parent / '.emscripten'))
    if not env.get('EMSDK_PYTHON') and config.is_file():
        for line in config.read_text().splitlines():
            if line.startswith('PYTHON = '):
                value = ast.literal_eval(line.split('=', 1)[1].strip())
                env['EMSDK_PYTHON'] = value.replace('$CFGDIR', str(config.parent))
                break
    return env


def dependency_archives(profile, tools, env, build, jobs):
    archives = []
    extra_sources = []
    for dependency in profile['dependencies']:
        extra_sources.extend(dependency.get('source_files', []))
        archive = dependency.get('archive')
        if not archive:
            continue
        archive = ROOT / archive
        if not archive.exists():
            name = dependency['name']
            log = build / f'dependency-{name}.log'
            print(f'wasm-build: building {name}', flush=True)
            if 'configure' in dependency:
                source = ROOT / dependency['source_dir']
                if not (source / 'configure').exists():
                    raise RuntimeError(f'missing {name} source at {source}')
                dep_env = dict(env)
                dep_env.update(dependency.get('configure_environment', {}))
                dep_env.update(CC=str(tools / 'emcc'), AR=str(tools / 'emar'),
                               RANLIB=str(tools / 'emranlib'),
                               CFLAGS=' '.join(profile['flags']))
                run_logged([str(source / 'configure'), *dependency['configure']],
                           log, dep_env, source)
                run_logged(['make', '-C', dependency.get('make_dir', '.'),
                            f'-j{jobs}', dependency['make_target']],
                           log, dep_env, source)
            else:
                destination = build / name
                flags = ' '.join(profile['flags'])
                run_logged([str(tools / 'emcmake'), 'cmake', '-S',
                            str(ROOT / dependency['cmake_source']), '-B',
                            str(destination), '-DCMAKE_BUILD_TYPE=MinSizeRel',
                            f'-DCMAKE_C_FLAGS={flags}', f'-DCMAKE_CXX_FLAGS={flags}',
                            *dependency.get('cmake_flags', [])], log, env)
                run_logged(['cmake', '--build', str(destination),
                            '--parallel', str(jobs)], log, env)
        archives.append(str(archive))
    return archives, extra_sources


def compile_source(source, profile, tools, env, build):
    path = ROOT / source
    # encode the full relative path so equal basenames cannot overwrite objects.
    object_file = build / 'obj' / (source.replace('/', '__') + '.o')
    log = build / 'logs' / (source.replace('/', '__') + '.log')
    cxx = path.suffix in ('.cpp', '.cc')
    flags = profile['flags'] if cxx else [f for f in profile['flags'] if f != '-fno-rtti']
    command = [str(tools / ('em++' if cxx else 'emcc')),
               '-std=c++17' if cxx else '-std=c17', *flags,
               *('-D' + define for define in profile['defines']),
               *('-I' + str(ROOT / include) for include in profile['includes']),
               '-c', str(path), '-o', str(object_file)]
    try:
        run_logged(command, log, env)
    except RuntimeError:
        return None, str(log)
    return str(object_file), None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    profile = json.loads((ROOT / 'build_lambda_config.json').read_text())['platforms']['lambda-wasm']
    compiler = compiler_path()
    if not compiler.is_file():
        raise RuntimeError('Emscripten is missing; set EMCC or EMSDK')
    tools = compiler.parent
    build = ROOT / profile['build_dir']
    build.mkdir(parents=True, exist_ok=True)
    (build / 'obj').mkdir(exist_ok=True)
    (build / 'logs').mkdir(exist_ok=True)
    env = compiler_environment(tools, build)
    # the SDK launcher selects its own compatible Python from .emscripten;
    # the build driver also runs on older system Python installations.
    version = subprocess.check_output([str(compiler), '--version'],
                                      env=env, text=True).splitlines()[0]
    archives, extra_sources = dependency_archives(profile, tools, env, build, args.jobs)
    sources = profile['source_files'] + extra_sources
    manifest = {'compiler': version, 'profile': 'lambda-wasm', 'sources': sources,
                'profile_sha256': hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest()}
    (build / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    objects = []
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = pool.map(lambda source: compile_source(source, profile, tools, env, build), sources)
        for index, (object_file, failure) in enumerate(results, 1):
            if failure:
                failures.append(failure)
            else:
                objects.append(object_file)
            if index % 25 == 0 or index == len(sources):
                print(f'wasm-build: compiled {index}/{len(sources)}, failures={len(failures)}', flush=True)
    (build / 'failures.json').write_text(json.dumps(failures, indent=2) + '\n')
    if failures:
        raise RuntimeError(f'{len(failures)} compilation failures; see {build / "failures.json"}')
    # publish only after a complete link; partial objects are never size results.
    staged = build / 'lambda-wasm.mjs'
    run_logged([str(tools / 'em++'), *profile['linker_flags'], *objects, *archives,
                '-o', str(staged)], build / 'link.log', env)
    output = ROOT / profile['output']
    output.parent.mkdir(parents=True, exist_ok=True)
    sizes = {}
    for suffix in ('.mjs', '.wasm'):
        artifact = staged.with_suffix(suffix)
        data = artifact.read_bytes()
        shutil.copy2(artifact, output.with_suffix(suffix))
        sizes[suffix] = {'bytes': len(data), 'gzip_bytes': len(gzip.compress(data, mtime=0))}
    report = dict(manifest, sizes=sizes)
    output.with_suffix('.size.json').write_text(json.dumps(report, indent=2) + '\n')
    print('wasm-size: ' + json.dumps(sizes))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f'wasm-build: {error}', file=sys.stderr)
        sys.exit(1)
