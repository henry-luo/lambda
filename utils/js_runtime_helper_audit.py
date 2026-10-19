#!/usr/bin/env python3
"""Conservative JS native-helper dependency audit (D8.1.3v25).

Requires Xcode libclang on macOS. Includes function/address/data dependencies,
macro expansion, actual MIR-emitter strings, host exports and test references.
The import registry is an inventory, never a liveness root. Unreached functions
are review candidates, not automatically removable. ast-grep checks definitions
hidden by preprocessing or unincluded headers. Outputs stay in temp/.
"""
import argparse
import concurrent.futures
import ctypes as c
import functools
import hashlib
import json
import re
import shutil
import subprocess
from collections import Counter, defaultdict, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JS = ROOT / 'lambda/js'
NON_HELPERS = {
    'js_ast_children.cpp', 'js_c_ast_helpers.cpp', 'js_c_parser.cpp',
    'js_direct_scope.cpp', 'js_early_errors.cpp', 'js_emit_ast_dump.cpp',
    'js_interp.cpp', 'js_module_ast_prebuild.cpp', 'js_scope.cpp',
    'mvp_lmd_mir.cpp',
}
MIXED_SOURCES = {'js_interp.cpp', 'mvp_lmd_mir.cpp'}
SOURCE_SUFFIXES = {'.cpp', '.c', '.cc', '.cxx', '.h', '.hpp', '.mm', '.def'}
LEX = re.compile(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/''', re.S)


def words(text):
    # Preserve string operands: generated MIR imports name their callees there.
    clean = LEX.sub(lambda m: ' ' if m[0].startswith(('/',)) else m[0], text)
    return set(re.findall(r'\b[A-Za-z_]\w*\b', clean))


def lexical_inventory(paths, known):
    rule = '''id: js-helper-body
language: cpp
severity: info
message: definition
rule:
  kind: function_definition
  has:
    kind: function_declarator
    has:
      any:
        - { kind: identifier, pattern: $NAME }
        - { kind: field_identifier, pattern: $NAME }
'''
    command = ['ast-grep', 'scan', '--inline-rules', rule, '--json=stream']
    result = subprocess.run(command + paths, cwd=ROOT, capture_output=True, text=True, check=True)
    normalized = {(f, n.lstrip('~').split('<')[0]) for f, n in known}
    missing = []
    for line in result.stdout.splitlines():
        row = json.loads(line)
        name = row['metaVariables']['single']['NAME']['text']
        filename = row['file']
        if ((filename, name) in normalized or name.isupper() or
                Path(filename).name in MIXED_SOURCES or
                re.search(r'=\s*(?:delete|default)\s*;\s*$', row['text'])):
            continue
        start = row['range']['start']['line'] + 1
        guards = []
        for source_line in (ROOT / filename).read_text().splitlines()[:start - 1]:
            directive = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', source_line)
            if not directive:
                continue
            kind, condition = directive.groups()
            if kind in ('if', 'ifdef', 'ifndef'):
                guards.append(kind + condition)
            elif kind in ('elif', 'else') and guards:
                guards[-1] = kind + condition
            elif kind == 'endif' and guards:
                guards.pop()
        missing.append({'file': filename, 'start': start, 'name': name,
            'category': 'conditional-build' if guards else 'unparsed-review',
            'guards': guards})
    return missing

class Cursor(c.Structure):
    _fields_ = [('kind', c.c_uint), ('xdata', c.c_int), ('data', c.c_void_p * 3)]
class String(c.Structure):
    _fields_ = [('data', c.c_void_p), ('flags', c.c_uint)]
class Location(c.Structure):
    _fields_ = [('ptr', c.c_void_p * 2), ('integer', c.c_uint)]
class Range(c.Structure):
    _fields_ = [('ptr', c.c_void_p * 2), ('begin', c.c_uint), ('end', c.c_uint)]
VISITOR = c.CFUNCTYPE(c.c_uint, Cursor, Cursor, c.c_void_p)


@functools.lru_cache(maxsize=1)
def clang_configuration():
    compiler = Path(subprocess.check_output(['xcrun', '--find', 'clang'], text=True).strip())
    sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
    args = ['-x', 'c++', '-std=c++17', '-DNDEBUG', '-DUTF8PROC_STATIC', '-Wno-c++11-narrowing',
        '-I' + str(ROOT), '-I' + str(ROOT / 'lambda/mir'), '-I' + str(ROOT / 'lib/re2')]
    if shutil.which('brew'):
        prefix = subprocess.check_output(['brew', '--prefix'], text=True).strip()
        args.append('-I' + str(Path(prefix) / 'include'))
    args += ['-isysroot', sdk]
    return str(compiler.parent.parent / 'lib/libclang.dylib'), args


def parse(path):
    library, args = clang_configuration()
    lib = c.CDLL(library)
    def api(name, result, args):
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = result, args
        return fn
    create = api('clang_createIndex', c.c_void_p, [c.c_int, c.c_int])
    dispose_index = api('clang_disposeIndex', None, [c.c_void_p])
    parse_tu = api('clang_parseTranslationUnit', c.c_void_p,
        [c.c_void_p, c.c_char_p, c.POINTER(c.c_char_p), c.c_int, c.c_void_p, c.c_uint, c.c_uint])
    dispose_tu = api('clang_disposeTranslationUnit', None, [c.c_void_p])
    get_root = api('clang_getTranslationUnitCursor', Cursor, [c.c_void_p])
    visit = api('clang_visitChildren', c.c_uint, [Cursor, VISITOR, c.c_void_p])
    get_loc = api('clang_getCursorLocation', Location, [Cursor])
    get_span = api('clang_getCursorExtent', Range, [Cursor])
    span_start = api('clang_getRangeStart', Location, [Range])
    span_end = api('clang_getRangeEnd', Location, [Range])
    get_location = api('clang_getExpansionLocation', None,
        [Location, c.POINTER(c.c_void_p), c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint)])
    get_filename = api('clang_getFileName', String, [c.c_void_p])
    get_spelling = api('clang_getCursorSpelling', String, [Cursor])
    get_linkage = api('clang_getCursorLinkage', c.c_uint, [Cursor])
    get_storage = api('clang_Cursor_getStorageClass', c.c_uint, [Cursor])
    get_usr = api('clang_getCursorUSR', String, [Cursor])
    get_mangling = api('clang_Cursor_getMangling', String, [Cursor])
    get_referenced = api('clang_getCursorReferenced', Cursor, [Cursor])
    get_template = api('clang_getSpecializedCursorTemplate', Cursor, [Cursor])
    overload_count = api('clang_getNumOverloadedDecls', c.c_uint, [Cursor])
    overload_at = api('clang_getOverloadedDecl', Cursor, [Cursor, c.c_uint])
    is_definition = api('clang_isCursorDefinition', c.c_uint, [Cursor])
    get_cstr = api('clang_getCString', c.c_char_p, [String])
    dispose_string = api('clang_disposeString', None, [String])
    num_diag = api('clang_getNumDiagnostics', c.c_uint, [c.c_void_p])
    get_diag = api('clang_getDiagnostic', c.c_void_p, [c.c_void_p, c.c_uint])
    diag_text = api('clang_formatDiagnostic', String, [c.c_void_p, c.c_uint])
    diag_severity = api('clang_getDiagnosticSeverity', c.c_uint, [c.c_void_p])
    dispose_diag = api('clang_disposeDiagnostic', None, [c.c_void_p])
    def string(value):
        data = get_cstr(value)
        result = data.decode(errors='replace') if data else ''
        dispose_string(value)
        return result
    def location(value):
        file, line, column, offset = c.c_void_p(), c.c_uint(), c.c_uint(), c.c_uint()
        get_location(value, c.byref(file), c.byref(line), c.byref(column), c.byref(offset))
        return (string(get_filename(file)) if file else '', line.value, offset.value)
    argv = (c.c_char_p * len(args))(*(arg.encode() for arg in args))
    index = create(0, 0)
    tu = parse_tu(index, str(path).encode(), argv, len(args), None, 0, 0x200)
    if not tu:
        raise RuntimeError('cannot parse ' + str(path))
    diagnostics = []
    for i in range(num_diag(tu)):
        diagnostic = get_diag(tu, i)
        if diag_severity(diagnostic) >= 3:
            diagnostics.append(string(diag_text(diagnostic, 0)))
        dispose_diag(diagnostic)
    records, declarations = [], []
    @VISITOR
    def top(cursor, parent, data):
        filename, line, offset = location(get_loc(cursor))
        if filename and Path(filename).resolve() != path.resolve():
            source = Path(filename).resolve()
            # header bodies contribute dependency edges, never implementation LOC.
            if not (source.is_relative_to(JS) and source.suffix in ('.h', '.hpp')):
                return 1
        if cursor.kind in (8, 21, 24, 25, 26, 30) and not is_definition(cursor):
            span = get_span(cursor)
            start, end = location(span_start(span)), location(span_end(span))
            declarations.append({'usr': string(get_usr(cursor)),
                'name': string(get_spelling(cursor)),
                'file': str(Path(filename).resolve().relative_to(ROOT)),
                'start': start[1], 'end': end[1],
                'begin_offset': start[2], 'end_offset': end[2]})
            return 1
        if cursor.kind in (8, 21, 24, 25, 26, 30) and is_definition(cursor):
            kind = 'function'
        elif cursor.kind == 9 and is_definition(cursor) and parent.kind not in (8, 21, 24, 25, 26, 30):
            kind = 'data'
        else:
            return 2
        references = set()
        reference_names = {}
        initializer_calls = []
        def add_reference(target):
            if target.kind not in (8, 9, 21, 24, 25, 26, 30):
                return
            template = get_template(target)
            if template.kind == 30:
                target = template
            usr = string(get_usr(target))
            if usr:
                references.add(usr)
                reference_names[usr] = string(get_spelling(target))
        @VISITOR
        def body(child, parent, data):
            if child.kind == 103:
                initializer_calls.append(True)
            target = get_referenced(child)
            add_reference(target)
            if child.kind == 49:
                for i in range(overload_count(child)):
                    add_reference(overload_at(child, i))
            return 2
        visit(cursor, body, None)
        span = get_span(cursor)
        start, end = location(span_start(span)), location(span_end(span))
        records.append({'usr': string(get_usr(cursor)), 'name': string(get_spelling(cursor)),
            'file': str(Path(filename).resolve().relative_to(ROOT)), 'kind': kind,
            'start': start[1], 'end': end[1], 'begin_offset': start[2], 'end_offset': end[2],
            'linkage': get_linkage(cursor), 'storage': get_storage(cursor),
            'cursor_kind': cursor.kind, 'reference_names': reference_names,
            'c_abi': string(get_mangling(cursor)) in
                (string(get_spelling(cursor)), '_' + string(get_spelling(cursor))),
            'initializer_calls': bool(initializer_calls),
            'references': sorted(references)})
        return 1
    visit(get_root(tu), top, None)
    result = {'file': str(path.relative_to(ROOT)), 'diagnostics': diagnostics,
              'records': records, 'declarations': declarations}
    dispose_tu(tu)
    dispose_index(index)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'temp/js_runtime_helper_audit')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT / 'temp'):
        parser.error('--out must be under the repository temp/ directory')
    out.mkdir(parents=True, exist_ok=True)
    sources = sorted(p for p in JS.glob('*.cpp') if p.name not in NON_HELPERS)
    sources += sorted(p for p in (JS / 'mvp-lmd').glob('*.cpp') if p.name not in NON_HELPERS)
    sources += [JS / 'js_well_known_names.c']
    sources += [JS / 'js_interp.cpp', JS / 'mvp-lmd/mvp_lmd_mir.cpp']
    digest = hashlib.sha256()
    digest.update(json.dumps(clang_configuration()).encode())
    for path in sorted(p for p in JS.rglob('*') if p.suffix in SOURCE_SUFFIXES):
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    fingerprint = digest.hexdigest()
    cache = out / 'parsed.json'
    parser_version = 4
    if (cache.exists() and json.loads(cache.read_text()).get('fingerprint') == fingerprint
            and json.loads(cache.read_text()).get('parser_version') == parser_version):
        parsed = json.loads(cache.read_text())['files']
    else:
        parsed = []
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
            for result in pool.map(parse, sources):
                parsed.append(result)
                print(result['file'], len(result['records']), 'records',
                      len(result['diagnostics']), 'errors', flush=True)
        cache.write_text(json.dumps({'fingerprint': fingerprint, 'parser_version': parser_version,
                                    'files': parsed}, indent=2))
    nodes = {}
    for unit in parsed:
        for record in unit['records']:
            if record['usr']:
                nodes.setdefault(record['usr'], record)
    by_name = defaultdict(list)
    for usr, node in nodes.items():
        by_name[node['name']].append(usr)
        # C USRs omit the C++ parameter encoding (including Mach-O's prefix).
        node['c_abi'] = usr.startswith('c:@F@') and '#' not in usr
        node['counted'] = (Path(node['file']).name not in MIXED_SOURCES or node['c_abi'])
    # Dependent overloads can refer to an instantiated declaration without a
    # definition cursor. Conservatively connect it to the named helper family.
    for node in nodes.values():
        missing = [r for r in node['references'] if r not in nodes]
        for reference in missing:
            node['references'].extend(by_name.get(node['reference_names'][reference], ()))
        node['references'] = sorted(set(node['references']))
    import_inventories = [node for node in nodes.values()
        if node['kind'] == 'data' and node['file'].endswith('mvp_lmd_mir.cpp')
        and node['name'] == 'imports']
    for node in import_inventories:
        node['references'] = []
    source_set = {p.relative_to(ROOT).as_posix() for p in sources}
    production, tests = defaultdict(set), defaultdict(set)
    for path in (JS / 'mvp-lmd/mvp_lmd_mir.cpp', JS / 'js_interp.cpp'):
        source = path.read_text()
        for literal in LEX.finditer(source):
            if not literal[0].startswith('"'):
                continue
            byte_offset = len(source[:literal.start()].encode())
            if any(table['file'] == str(path.relative_to(ROOT)) and
                    table['begin_offset'] <= byte_offset < table['end_offset']
                    for table in import_inventories):
                continue
            for name in words(literal[0]) & by_name.keys():
                for usr in by_name[name]:
                    production[usr].add(str(path.relative_to(ROOT)) + ' (string operand)')
    paths = subprocess.check_output(['rg', '--files', 'lambda', 'radiant', 'lib', 'test'],
                                    cwd=ROOT, text=True).splitlines()
    for filename in paths:
        path = ROOT / filename
        if path.suffix not in SOURCE_SUFFIXES or filename in source_set:
            continue
        if filename in ('lambda/runtime/sys_func_registry.c', 'lambda/runtime/no_js_stubs.cpp'):
            continue
        if filename.startswith('lambda/js/') and path.suffix in ('.h', '.hpp'):
            continue  # helper declarations are not consumers
        if ('/tree-sitter' in filename or filename.startswith('lambda/mir/') or
                filename.startswith('lib/re2/')):
            continue
        target = tests if filename.startswith('test/') else production
        for name in words(path.read_text(errors='replace')) & by_name.keys():
            for usr in by_name[name]:
                target[usr].add(filename)
    # ABI exports can be loaded from native modules even without a literal call.
    exports = (ROOT / 'lambda/jube/jube_host_exports.txt').read_text()
    for name in words(exports) & by_name.keys():
        for usr in by_name[name]:
            production[usr].add('lambda/jube/jube_host_exports.txt')
    # Destruction is implicit at scope exits; there need not be a DeclRefExpr.
    for usr, node in nodes.items():
        if node['kind'] == 'function' and not node['counted']:
            production[usr].add('compiler/interpreter implementation (consumer)')
        elif node['cursor_kind'] in (24, 25) or node['name'].startswith('~'):
            production[usr].add('implicit C++ lifetime (conservative retention)')
        elif node['kind'] == 'data' and node['initializer_calls']:
            production[usr].add('global initializer (conservative retention)')
    incoming = defaultdict(set)
    for usr, node in nodes.items():
        for callee in node['references']:
            if callee in nodes and callee != usr:
                incoming[callee].add(usr)
    def reach(roots):
        parent, queue = {usr: None for usr in roots}, deque(roots)
        while queue:
            caller = queue.popleft()
            for callee in nodes[caller]['references']:
                if callee in nodes and callee not in parent:
                    parent[callee] = caller
                    queue.append(callee)
        return parent
    live, test_live = reach(production), reach(tests)
    source_bytes = {}
    # Release preprocessing hides diagnostic/platform calls inside otherwise
    # live bodies. Retain their named callees and follow their normal edges.
    while True:
        additions = False
        for usr in list(live):
            node = nodes[usr]
            if node['kind'] != 'function':
                continue
            filename = node['file']
            if filename not in source_bytes:
                source_bytes[filename] = (ROOT / filename).read_bytes()
            body = source_bytes[filename][node['begin_offset']:node['end_offset']].decode(errors='replace')
            if not re.search(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b', body, re.M):
                continue
            for name in words(body) & by_name.keys():
                for target in by_name[name]:
                    if target not in live:
                        production[target].add(f"conditional source in {filename}:{node['start']}")
                        additions = True
        if not additions:
            break
        live = reach(production)
    rows = []
    for usr, node in nodes.items():
        if node['kind'] != 'function' or not node['counted']:
            continue
        category = 'production-referenced' if usr in live else (
            'test-only' if usr in test_live else 'unreached-review')
        chain, current = [], usr
        if usr in live:
            while current is not None:
                chain.append(nodes[current]['name'])
                current = live[current]
        rows.append({**node, 'category': category,
            'direct_consumers': sorted(production.get(usr, ())),
            'test_consumers': sorted(tests.get(usr, ())),
            'incoming': sorted(incoming.get(usr, ())), 'root_chain': chain})
    rows.sort(key=lambda r: (r['file'], r['start'], r['usr']))
    lexical_paths = sorted(source_set | {str(p.relative_to(ROOT))
        for p in JS.rglob('*') if p.suffix in ('.h', '.hpp')})
    lexical = lexical_inventory(lexical_paths, {(r['file'], r['name']) for r in rows})
    diagnostics = {u['file']: u['diagnostics'] for u in parsed if u['diagnostics']}
    report = {'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'fingerprint': fingerprint, 'method': __doc__, 'clang': clang_configuration(),
        'counts': dict(Counter(r['category'] for r in rows + lexical)), 'diagnostics': diagnostics,
        'sources': sorted(source_set), 'functions': rows, 'conditional_functions': lexical}
    (out / 'audit.json').write_text(json.dumps(report, indent=2))
    lines = ['file\tline\tfunction\tcategory\troot_chain\tdirect_consumers']
    lines += ['\t'.join((r['file'], str(r['start']), r['name'], r['category'],
                           ' <- '.join(r['root_chain']), ';'.join(r['direct_consumers']))) for r in rows]
    lines += ['\t'.join((r['file'], str(r['start']), r['name'], r['category'],
                         ';'.join(r['guards']), '')) for r in lexical]
    (out / 'functions.tsv').write_text('\n'.join(lines) + '\n')
    candidates = [r for r in rows + lexical if r['category'] not in
                  ('production-referenced', 'conditional-build')]
    (out / 'candidates.json').write_text(json.dumps(candidates, indent=2))
    print(json.dumps({'counts': report['counts'], 'diagnostics': diagnostics}, indent=2), flush=True)
    return 1 if diagnostics else 0


if __name__ == '__main__':
    raise SystemExit(main())
