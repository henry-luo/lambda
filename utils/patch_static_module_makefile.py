#!/usr/bin/env python3
"""Append exact static archives after Lambda's generated executable libraries."""

import os
import sys
from pathlib import Path

from split_paths import resolve_split_paths, thorvg_openmp_archives, use_input_scripts
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
    """Replace every generated provider reference in both link and prerequisites."""
    input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', Path(root).parent / 'lambda-input')).resolve()
    radiant_root = Path(os.environ.get('RADIANT_ROOT', Path(root).parent / 'radiant')).resolve()
    profiles = ('debug', 'debug_asan', 'release', 'release_profile', 'coverage')
    providers = {
        '../lib/liblambda-data-cpp.a': input_root / 'build/lib/$(notdir $(OBJDIR))/liblambda-input.a',
        '../lib/libradiant-cpp.a': radiant_root / 'build/lib/$(notdir $(OBJDIR))/libradiant.a',
    }
    for profile in profiles:
        providers[f'../lib/native/{profile}/liblambda-lib-cpp.a'] = input_root / f'build/lib/{profile}/liblambda-lib.a'
    for original, archive in providers.items():
        if original not in content:
            raise RuntimeError(f'missing generated provider reference: {original}')
        content = content.replace(original, str(archive))
    return content


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
                if dependency in visited_external or library.get('link') != 'static':
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
