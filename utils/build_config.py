"""Load the shared JSON build descriptions without duplicating source lists."""

import json
from pathlib import Path


def load_build_config(path, ancestors=()):
    path = Path(path).resolve()
    if path in ancestors:
        raise ValueError(f'cyclic build config import: {path}')
    config = json.loads(path.read_text(encoding='utf-8'))
    merged = {}
    for imported in config.pop('config_imports', []):
        _merge(merged, load_build_config(path.parent / imported, (*ancestors, path)))
    _merge(merged, config)
    for key in ('targets', 'libraries', 'dev_libraries'):
        names = [item['name'] for item in merged.get(key, []) if isinstance(item, dict)]
        if len(names) != len(set(names)):
            raise ValueError(f'duplicate {key} name in {path}')
    return merged


def _merge(destination, source):
    for key, value in source.items():
        if key in destination and isinstance(value, dict):
            _merge(destination[key], value)
        elif key in destination and isinstance(value, list):
            destination[key].extend(value)
        else:
            destination[key] = value


def resolve_source_sets(target, system, architecture):
    """Resolve one target or generated-header selection exactly once."""
    for group in target.pop('source_sets', []):
        if system in group.get('platforms', [system]) and \
                architecture in group.get('architectures', [architecture]):
            _merge(target, {key: value for key, value in group.items()
                            if key not in ('platforms', 'architectures')})
