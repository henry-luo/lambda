#!/usr/bin/env python3
"""Append exact static archives after Lambda's generated executable libraries."""

import json
import os
import sys


def platform_libraries(target, is_macos):
    libraries = list(target.get('libraries', []))
    if is_macos:
        override = target.get('macos', {})
        for name in override.get('additional_libraries', []):
            if name not in libraries:
                libraries.append(name)
        excluded = set(override.get('exclude_libraries', []))
        libraries = [name for name in libraries if name not in excluded]
    return libraries


def main():
    if sys.platform != 'darwin':
        return 0
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, 'build_lambda_config.json'), encoding='utf-8') as source:
        config = json.load(source)
    targets = {target['name']: target for target in config.get('targets', []) if 'name' in target}
    executable = targets.get('lambda-exe')
    if not executable:
        return 0

    external = {library['name']: library for library in config.get('libraries', []) if 'name' in library}
    for library in config.get('platforms', {}).get('macos', {}).get('libraries', []):
        if 'name' in library:
            external[library['name']] = library

    archives = []
    visited_targets = set()
    visited_external = set()

    def visit(target):
        name = target.get('name')
        if name in visited_targets:
            return
        visited_targets.add(name)
        for dependency in platform_libraries(target, True):
            library = external.get(dependency)
            if library:
                if dependency in visited_external or library.get('link') != 'static':
                    continue
                visited_external.add(dependency)
                archive = library.get('lib', '')
                if archive and not archive.startswith('-'):
                    archives.append(archive if os.path.isabs(archive) else os.path.join(root, archive))
                continue
            child = targets.get(dependency)
            if child:
                visit(child)

    visit(executable)
    makefile_path = os.path.join(root, 'build', 'premake', 'lambda-exe.make')
    with open(makefile_path, encoding='utf-8') as source:
        content = source.read()
    marker = '$(ALL_LDFLAGS) $(LIBS)'
    replacement = '$(ALL_LDFLAGS) $(LIBS) $(LAMBDA_STATIC_LATE_ARCHIVES)'
    if marker not in content:
        raise RuntimeError('lambda-exe Makefile link command changed; cannot append static archives')
    content = content.replace(marker, replacement, 1)
    archive_line = 'LAMBDA_STATIC_LATE_ARCHIVES += ' + ' '.join(archives) + '\n'
    content = content.replace('LINKCMD = ', archive_line + 'LINKCMD = ', 1)
    with open(makefile_path, 'w', encoding='utf-8') as output:
        output.write(content)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
