"""Resolve source and header paths owned by the sibling projects."""

import os
import shlex
import subprocess
import sys
from pathlib import Path


PATH_FIELDS = frozenset({
    'source_dirs', 'extra_source_dirs', 'remove_source_dirs', 'source_files',
    'extra_source_files', 'remove_source_files', 'sources', 'source_patterns',
    'exclude_patterns', 'exclude_source_files', 'extra_exclude_source_files',
    'exclude_source_globs', 'additional_source_files', 'additional_sources',
    'additional_files', 'objcxx_source_files', 'files', 'includes', 'source',
    'include', 'info_plist', 'memtrack_poison_dirs',
})


def sibling_roots():
    root = Path(__file__).resolve().parent.parent
    return (Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input')).resolve(),
            Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve())


def thorvg_openmp_archives(required=False):
    """Use the OpenMP runtime recorded by the owner-built ThorVG archive."""
    pkg_dir = sibling_roots()[0] / 'build/deps/thorvg/lib/pkgconfig'
    metadata = pkg_dir / 'thorvg.pc'
    if not metadata.is_file() and not required:
        return []
    env = dict(os.environ)
    env['PKG_CONFIG_PATH'] = str(pkg_dir) + os.pathsep + env.get('PKG_CONFIG_PATH', '')
    flags = shlex.split(subprocess.check_output(
        ['pkg-config', '--libs', '--static', 'thorvg'], env=env, text=True))
    archives = []
    for flag in flags:
        if flag.endswith('/libomp.dylib'):
            archive = Path(flag).with_suffix('.a')
            if not archive.is_file():
                raise RuntimeError(f'ThorVG enabled OpenMP but its static runtime is missing: {archive}')
            archives.append(str(archive))
        elif flag in ('-lomp', '-lgomp'):
            archives.append(flag)
    return archives


def use_input_scripts():
    scripts = str(sibling_roots()[0] / 'scripts')
    if scripts not in sys.path:
        sys.path.insert(0, scripts)


def logical_project_path(path, root):
    """Return the owner-relative name used by source audits and reports."""
    path = Path(path)
    for owner in (*sibling_roots(), Path(root)):
        if path.is_relative_to(owner):
            return path.relative_to(owner)
    raise ValueError(f'path is outside the project roots: {path}')


def project_source_path(relative, root=None, roots=None):
    """Resolve a logical source name to its owning checkout."""
    root = Path(root) if root is not None else Path(__file__).resolve().parent.parent
    input_root, radiant_root = roots or sibling_roots()
    path = Path(relative)
    if path.parts and path.parts[0] == 'lib':
        return input_root / path
    if path.parts and path.parts[0] == 'radiant':
        return radiant_root / path
    if len(path.parts) >= 2 and path.parts[0] == 'lambda':
        if path.parts[1] in ('input', 'io', 'network', 'format'):
            return input_root / path
        if path.parts[1] == 'core' or path == Path('lambda/lambda.h'):
            local = root / path
            candidate = input_root / path
            if local.is_symlink() or (not local.exists() and candidate.exists()) or \
                    ('*' in path.name and candidate.parent.is_dir()):
                return candidate
    if path.parts and path.parts[0] == 'test':
        local = root / path
        if local.is_symlink():
            target = local.resolve()
            if target.is_relative_to(input_root) or target.is_relative_to(radiant_root):
                return target
        if not local.exists():
            for owner in (input_root, radiant_root):
                candidate = owner / path
                if candidate.exists():
                    return candidate
    return root / path


def resolve_split_paths(config):
    """Map owned paths in imported manifests without changing target names."""
    root = Path(__file__).resolve().parent.parent
    input_root, radiant_root = sibling_roots()

    def visit(value, field=None):
        if isinstance(value, dict):
            return {key: visit(item, key) for key, item in value.items()}
        if isinstance(value, list):
            return [visit(item, field) for item in value]
        if isinstance(value, str):
            value = value.replace('${input_root}', str(input_root)).replace('${radiant_root}', str(radiant_root))
        if isinstance(value, str) and field in PATH_FIELDS:
            if field == 'source' and not value.startswith('test/'):
                # Test manifests historically spell paths relative to test/.
                candidate = project_source_path('test/' + value, root, (input_root, radiant_root))
                if candidate.is_file() and not candidate.is_relative_to(root):
                    return Path(os.path.relpath(candidate, root)).as_posix()
                return value
            path = project_source_path(value, root, (input_root, radiant_root))
            if not path.is_relative_to(root):
                return Path(os.path.relpath(path, root)).as_posix()
        return value

    result = visit(config)
    # Canonical includes resolve against the owning project, including overrides.
    includes = result.setdefault('includes', [])
    for directory in (input_root, radiant_root, input_root / 'test', input_root / 'test/css', radiant_root / 'test'):
        path = Path(os.path.relpath(directory, root)).as_posix()
        if path not in includes:
            includes.append(path)
    return result
