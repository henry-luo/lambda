#!/usr/bin/env python3
"""Check imported build contracts, native probes, and a real dependency consumer."""

import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile
import unittest

from build_config import load_build_config, resolve_source_sets
from build_dependencies import CompilerProbe, expand, generate_premake, projects_for, render_template

ROOT = Path(__file__).resolve().parents[1]


class DependencyBuildTest(unittest.TestCase):
    def setUp(self):
        (ROOT / 'temp').mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / 'temp')
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def test_imports_preserve_sources_and_reject_cycles_and_duplicates(self):
        leaf = self.work / 'leaf.json'
        main = self.work / 'main.json'
        leaf.write_text(json.dumps({'targets': [{'name': 'provider'}], 'headers': {'A': 1},
                                    'description': 'dependency 源'}, ensure_ascii=False), encoding='utf-8')
        main.write_text(json.dumps({'config_imports': ['leaf.json'], 'targets': [{'name': 'host'}],
                                    'headers': {'B': 2}}))
        resolved = load_build_config(main)
        self.assertEqual([target['name'] for target in resolved['targets']], ['provider', 'host'])
        self.assertEqual(resolved['headers'], {'A': 1, 'B': 2})
        self.assertEqual(resolved['description'], 'dependency 源')
        main.write_text(json.dumps({'config_imports': ['leaf.json'], 'targets': [{'name': 'provider'}]}))
        with self.assertRaisesRegex(ValueError, 'duplicate targets'):
            load_build_config(main)
        leaf.write_text(json.dumps({'config_imports': ['main.json']}))
        with self.assertRaisesRegex(ValueError, 'cyclic'):
            load_build_config(main)

    def test_templates_disable_optional_features_and_require_metadata(self):
        template = '#cmakedefine HAVE_MISSING @MISSING@\n#cmakedefine01 HAVE_PRESENT\n#define VERSION "@VERSION@"\n'
        self.assertEqual(render_template(template, {'HAVE_PRESENT': 1, 'VERSION': '1.2'}),
                         '/* #undef HAVE_MISSING */\n#define HAVE_PRESENT 1\n#define VERSION "1.2"\n')
        with self.assertRaisesRegex(ValueError, 'missing template variable'):
            render_template('#define VERSION @VERSION@', {})

    def test_platform_selection_is_shared_and_idempotent(self):
        target = {'defines': {'BASE': 1}, 'source_sets': [
            {'platforms': ['windows'], 'defines': {'WINDOWS': 1}},
            {'architectures': ['arm64'], 'defines': {'ARM': 1}}]}
        resolve_source_sets(target, 'linux', 'arm64')
        resolve_source_sets(target, 'linux', 'arm64')
        self.assertEqual(target, {'defines': {'BASE': 1, 'ARM': 1}})

    def test_compiler_probes_distinguish_missing_features(self):
        compiler = os.environ.get('CC') or shutil.which('cc')
        probe = CompilerProbe(compiler, self.work, dict(os.environ, CPPFLAGS='-DLAMBDA_PROBE_ENV=1', CFLAGS='-Werror'))
        values = probe.values({'headers': {'HAVE_STDDEF_H': 'stddef.h', 'HAVE_MISSING': 'lambda_absent_header.h'},
                               'expressions': {'HAVE_ENV_FLAGS': 'LAMBDA_PROBE_ENV'},
                               'types': {'COUNT_TYPE': ['lambda_absent_type', 'size_t']},
                               'sizes': {'SIZEOF_CHAR': 'char'}})
        self.assertNotIn('HAVE_MISSING', values)
        self.assertEqual(values['HAVE_ENV_FLAGS'], 1)
        self.assertEqual(values['COUNT_TYPE'], 'size_t')
        self.assertEqual(values['SIZEOF_CHAR'], 1)

    def test_source_validation_and_native_archive_consumer(self):
        compiler = os.environ.get('CC') or shutil.which('cc')
        system = {'Darwin': 'macos', 'Linux': 'linux'}.get(platform.system(), 'windows')
        variables = {'source': str(self.work), 'build': str(self.work / 'build'),
                     'system': system, 'architecture': platform.machine()}
        (self.work / 'probe.cc').write_text('extern "C" int dependency_answer(void) { return 42; }\n')
        (self.work / 'app.c').write_text('int dependency_answer(void); int main(void) { return dependency_answer() != 42; }\n')
        recipe = expand({'projects': [{'name': 'provider', 'lang': 'c++17',
                                        'source_patterns': ['${source}/*.cc']}]}, variables)
        projects = projects_for(recipe, variables)
        script = self.work / 'premake5.lua'
        generate_premake(recipe, variables, projects, compiler, script)
        premake = os.environ.get('PREMAKE5_BIN') or shutil.which('premake5')
        env = dict(os.environ, TMPDIR=str(self.work))
        commands = [[premake, '--file=' + str(script), 'gmake'],
                    ['make', '-C', str(self.work / 'build/make'), 'config=release_native'],
                    [*shlex.split(compiler), str(self.work / 'app.c'), str(self.work / 'build/lib/libprovider.a'),
                     '-o', str(self.work / 'consumer.exe')], [str(self.work / 'consumer.exe')]]
        for command in commands:
            result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)
        recipe['projects'][0]['source_patterns'] = [str(self.work / '*.missing')]
        with self.assertRaisesRegex(ValueError, 'no dependency sources'):
            projects_for(recipe, variables)

    def test_make_selection_keeps_static_only_sources_without_building(self):
        (self.work / 'Makefile').write_text('WIN32RES = version.o\nOBJS = base.o $(WIN32RES)\nOBJS_STATIC = auth.o\nall:\n\t@false\n')
        for name in ('base.c', 'auth.c'):
            (self.work / name).write_text('/* source-selection fixture */\n')
        recipe = {'projects': [{'name': 'client', 'make_sources': {
            'directory': str(self.work), 'source_directory': str(self.work),
            'variables': ['OBJS', 'OBJS_STATIC'], 'make_variables': {'WIN32RES': ''}}}]}
        variables = {'build': str(self.work), 'system': 'macos', 'architecture': 'arm64'}
        projects = projects_for(recipe, variables)
        self.assertEqual([Path(path).name for path in projects[0]['source_files']], ['base.c', 'auth.c'])


if __name__ == '__main__':
    unittest.main()
