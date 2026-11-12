#!/usr/bin/env python3
"""Append exact static archives after Lambda's generated executable libraries."""

import os
import re
import sys
from pathlib import Path

from split_paths import optional_library_available, resolve_split_paths, thorvg_openmp_archives, use_input_scripts
use_input_scripts()
from build_config import load_build_config


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


def configured_archive_path(archive, root):
    # OBJDIR is set by each generated Make configuration, including profiles
    # with underscores such as debug_asan and release_profile.
    archive = archive.replace('%{cfg.platform}',
        '$(notdir $(patsubst %/,%,$(dir $(OBJDIR))))')
    archive = archive.replace('%{cfg.buildcfg}', '$(notdir $(OBJDIR))')
    return archive if os.path.isabs(archive) else os.path.join(root, archive)


def use_sibling_archives(content, root):
    """Replace provider links with owner archives and track their timestamps."""
    input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', Path(root).parent / 'lambda-input')).resolve()
    radiant_root = Path(os.environ.get('RADIANT_ROOT', Path(root).parent / 'radiant')).resolve()
    profile = '$(notdir $(OBJDIR))'
    archives = {
        'lambda-data-cpp': input_root / f'build/lib/{profile}/liblambda-input.a',
        'lambda-lib-cpp': input_root / f'build/lib/{profile}/liblambda-lib.a',
        'radiant-cpp': radiant_root / f'build/lib/{profile}/libradiant.a',
    }
    profiles = ('debug', 'debug_asan', 'release', 'release_profile', 'coverage')
    legacy = {
        '../lib/liblambda-data-cpp.a': archives['lambda-data-cpp'],
        '../lib/libradiant-cpp.a': archives['radiant-cpp'],
    }
    for name in profiles:
        legacy[f'../lib/native/{name}/liblambda-lib-cpp.a'] = input_root / f'build/lib/{name}/liblambda-lib.a'
    if all(original in content for original in legacy):
        for original, archive in legacy.items():
            content = content.replace(original, str(archive))
        return content

    # Without local Premake provider projects, gmake emits -l names instead of
    # paths. Substitute only complete linker tokens and add real prerequisites.
    lines = []
    found = set()
    for line in content.splitlines(keepends=True):
        selected = []
        if line.startswith('LIBS +='):
            tokens = line.split()
            for name, archive in archives.items():
                token = '-l' + name
                if token in tokens:
                    line = re.sub(r'(?<!\S)' + re.escape(token) + r'(?!\S)',
                                  lambda match: str(archive), line)
                    selected.append(str(archive))
                    found.add(name)
            if selected and len(selected) != len(archives):
                raise RuntimeError('incomplete sibling provider links in generated configuration')
        lines.append(line)
        if selected:
            lines.append('LDDEPS += ' + ' '.join(selected) + '\n')
    if found == set(archives):
        return ''.join(lines)

    # GNU linker groups may already contain the owner archive paths.
    expected = ('liblambda-input.a', 'liblambda-lib.a', 'libradiant.a')
    if all(name in content for name in expected):
        return content
    raise RuntimeError('missing generated sibling provider references')


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Imported sibling manifests also own transitive static dependencies.
    config = resolve_split_paths(load_build_config(os.path.join(root, 'build_lambda_config.json')))
    targets = {target['name']: target for target in config.get('targets', []) if 'name' in target}
    executable = targets.get('lambda-exe')
    if not executable:
        return 0

    external = {library['name']: library for library in config.get('libraries', []) if 'name' in library}
    platform_name = 'macos' if sys.platform == 'darwin' else ('windows' if os.environ.get('MSYSTEM') else 'linux')
    for library in config.get('platforms', {}).get(platform_name, {}).get('libraries', []):
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
        for dependency in platform_libraries(target, platform_name == 'macos'):
            library = external.get(dependency)
            if library:
                if (dependency in visited_external or library.get('link') != 'static' or
                        not optional_library_available(library)):
                    continue
                visited_external.add(dependency)
                archive = library.get('lib', '')
                if archive and not archive.startswith('-'):
                    archives.append(configured_archive_path(archive, root))
                continue
            child = targets.get(dependency)
            if child:
                visit(child)

    visit(executable)
    makefile_path = os.path.join(root, 'build', 'premake', 'lambda-exe.make')
    with open(makefile_path, encoding='utf-8') as source:
        content = source.read()
    if sys.argv[1:] == ['--sibling-archives']:
        content = use_sibling_archives(content, root)
    elif sys.argv[1:]:
        raise RuntimeError('usage: patch_static_module_makefile.py [--sibling-archives]')
    # Apple ld needs the dependency closure repeated after the first-party archives.
    if platform_name != 'macos':
        with open(makefile_path, 'w', encoding='utf-8') as output:
            output.write(content)
        return 0
    archives.extend(thorvg_openmp_archives(required=True))
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
