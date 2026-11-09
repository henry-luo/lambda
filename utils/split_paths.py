"""Resolve source and header paths owned by the sibling projects."""

import os
from pathlib import Path


PATH_FIELDS = frozenset({
    'source_dirs', 'extra_source_dirs', 'remove_source_dirs', 'source_files',
    'extra_source_files', 'remove_source_files', 'sources', 'source_patterns',
    'exclude_patterns', 'exclude_source_files', 'extra_exclude_source_files',
    'exclude_source_globs', 'additional_source_files', 'additional_sources',
    'additional_files', 'objcxx_source_files', 'files', 'includes',
    'include', 'info_plist', 'memtrack_poison_dirs',
})


def sibling_roots():
    root = Path(__file__).resolve().parent.parent
    return (Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input')).resolve(),
            Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve())


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
        if isinstance(value, str) and field in PATH_FIELDS:
            owner = value.partition('/')[0]
            if owner in ('lib', 'radiant'):
                path = project_source_path(value, root, (input_root, radiant_root))
                return Path(os.path.relpath(path, root)).as_posix()
        return value

    result = visit(config)
    # Canonical includes resolve against the owning project, including overrides.
    includes = result.setdefault('includes', [])
    for directory in (input_root, radiant_root):
        path = Path(os.path.relpath(directory, root)).as_posix()
        if path not in includes:
            includes.append(path)
    return result
