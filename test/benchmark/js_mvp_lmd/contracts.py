"""Versioned workload admission and source-closure freezing for MVP comparisons."""
from pathlib import Path
import re
import shutil

from benchmark_provenance import mask_lambda_source, sha256_file


def freeze_lambda_closure(entry, source_root, destination):
    """Resolve relative imports per S16.9.8; reject unresolved audit inputs."""
    source_root, destination = Path(source_root).resolve(), Path(destination).resolve()
    pending, seen, sources, annotated = [Path(entry).resolve()], set(), [], []
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        if not path.is_relative_to(source_root) or not path.is_file():
            raise ValueError('unresolved benchmark dependency: ' + str(path))
        relative = path.relative_to(source_root)
        frozen = destination / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, frozen)
        digest = sha256_file(path)
        sources.extend([{'reference': str(path), 'sha256': digest},
                        {'frozen': str(frozen), 'sha256': digest}])
        masked = mask_lambda_source(path.read_text())
        # Numeric annotations in imported helpers disqualify an untyped claim too.
        if re.search(r':\s*(?:int|int64|float|decimal|u32)\b|\)\s+(?:int|float)\s*[{=]', masked):
            annotated.append(str(relative))
        for match in re.finditer(r'\bimport\s+([^\n;]+)', masked):
            module = match[1].strip()
            if not re.fullmatch(r'(?:~~\.)*\.?[A-Za-z_]\w*(?:\.\w+)*', module) or not module.startswith(('.', '~~.')):
                raise ValueError('unsupported benchmark import audit: ' + module)
            parent = path.parent
            while module.startswith('~~.'):
                parent, module = parent.parent, module[3:]
            pending.append((parent / (module.lstrip('.').replace('.', '/') + '.ls')).resolve())
    return destination / Path(entry).resolve().relative_to(source_root), sources, sorted(annotated)


def havlak_contract(source, language):
    source = mask_lambda_source(source)
    if language == 'js':
        match = re.search(r'\.main\(\s*innerIterations\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,?\s*\)', source)
        if not re.search(r'innerBenchmarkLoop\(1\)', source):
            raise ValueError('unverified Havlak dummy-recognition count')
        values = (1, *map(int, match.groups())) if match else None
    else:
        matches = re.findall(r'lta_main\(\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\)', mask_lambda_source(source))
        values = tuple(map(int, matches[0])) if len(matches) == 1 else None
    if not values:
        raise ValueError('unrecognized Havlak work contract')
    dummy, extra, trees, loops, base = values
    return dict(version=1, dummy_recognitions=dummy, full_recognitions=extra + 1,
                trees=trees, loops=loops, base_loops=base, nodes=5213, loops_oracle=1605)


def require_same_work(left, right):
    if left != right:
        raise ValueError('work contracts differ, regardless of matching output: ' + str((left, right)))


def match_havlak(row, extra):
    """Version only frozen diagnostic sources; canonical contracts remain intact."""
    if extra not in (1, 50):
        raise ValueError('supported matched Havlak contracts use 1 or 50 extra recognitions')
    js = Path(row['commands']['candidate'][-1])
    ls = Path(row['commands']['lambda_untyped'][-1])
    for path, language in ((js, 'js'), (ls, 'lambda')):
        source = path.read_text()
        contract = havlak_contract(source, language)
        old = (r'(\.main\(\s*innerIterations,\s*)\d+' if language == 'js' else
               r'(lta_main\(\s*1,\s*)\d+')
        source, count = re.subn(old, lambda m: m[1] + str(extra), source)
        if count != 1 or contract['full_recognitions'] not in (2, 51):
            raise ValueError('Havlak source contract changed')
        path.write_text(source)
    work = havlak_contract(js.read_text(), 'js')
    require_same_work(work, havlak_contract(ls.read_text(), 'lambda'))
    row['id'] = f'awfy/havlak-matched-v1-r{extra}'
    row['work_contracts'] = {'candidate': work, 'lambda_untyped': work}
    row['lambda_comparability'] = 'matched work counts; port representations and algorithms differ'
    for source in row['sources']:
        if 'frozen' in source:
            source['sha256'] = sha256_file(source['frozen'])
