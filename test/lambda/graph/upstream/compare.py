#!/usr/bin/env python3
"""Compare Lambda CLI renders with semantic observations from upstream SVGs."""

from argparse import ArgumentParser
import base64
from concurrent.futures import ThreadPoolExecutor
import hashlib
import html
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

SUITE = Path(__file__).resolve().parent
ROOT = SUITE.parents[3]
OUTPUT = ROOT / 'temp/graph-upstream'
GOLDEN = SUITE / 'expected.json'
NUMBER = r'-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?'


def css_class(root, token):
    return [el for el in root.iter() if token in el.get('class', '').split()]


def role(root, name):
    return root.findall(f".//*[@data-graph-role='{name}']")


def words(value):
    return ' '.join(value.split())


def text(element):
    return words(' '.join(element.itertext()))


def attr(el, key):
    return el.get('data-' + key, '')


def arrow(source, target, start, end):
    if start and not end:
        return [target, source, False, True]
    return [source, target, start, end]


def d2_id(value):
    return json.loads(value) if value.startswith('"') else value


def topology(nodes, edges, labels=()):
    return {'nodes': sorted(nodes), 'edges': sorted(edges), 'edge_labels': sorted(filter(None, labels))}


def upstream_d2(root, case):
    nodes = {}
    edges = []
    labels = []
    for group in root.iterfind('.//{*}g'):
        token = group.get('class', '')
        if not re.fullmatch(r'[A-Za-z0-9+/=]+', token):
            continue
        try:
            name = html.unescape(base64.b64decode(token, validate=True).decode())
        except (ValueError, UnicodeError):
            continue
        if any(child.get('class') == 'shape' for child in group):
            label = group.find('{*}text')
            nodes[d2_id(name)] = label
        else:
            connection = next((el for el in css_class(group, 'connection') if el.get('d')), None)
            names = re.fullmatch(r'\((.*?) (?:->|<-|--|<->) (.*?)\)\[\d+\]', name)
            if connection is not None and names:
                edges.append(arrow(*map(d2_id, names.groups()), bool(connection.get('marker-start')),
                                   bool(connection.get('marker-end'))))
                labels += [text(label) for label in group.findall('{*}text')]
    report = topology(nodes, edges, labels)
    report['node_labels'] = {key: text(value) for key, value in nodes.items()}
    if case.stem == 'direction-right':
        report['horizontal_order'] = sorted(nodes, key=lambda key: float(nodes[key].get('x')))
    return report


def upstream_dot(root, case):
    names = {node.findtext('{*}title'): node for node in css_class(root, 'node')}
    structurizr = case.parent.name == 'structurizr'
    mapped = {key: text(node.find('.//{*}text')) if structurizr else key
              for key, node in names.items()}
    labels = []
    edges = []
    for edge in css_class(root, 'edge'):
        fr, op, to = re.split(r'(->|--)', edge.findtext('{*}title'), maxsplit=1)
        if structurizr:
            pair = [[mapped[fr]], [mapped[to]]]
        else:
            pair = [end.split(':', 1) for end in (fr, to)]
        edges.append(sorted(pair) if op == '--' else pair)
        labels += [text(label) for label in edge.iterfind('.//{*}text')]
    result = topology(mapped.values(), edges, labels)
    if not structurizr:
        clusters = css_class(root, 'cluster')
        result['clusters'] = sorted([c.findtext('{*}title'), *[text(x) for x in c.findall('{*}text')]]
                                    for c in clusters)
        if case.stem == 'clust':
            result['cluster_paint'] = {c.findtext('{*}title'):
                [c.find('{*}polygon').get('stroke'), bool(c.find('{*}polygon').get('stroke-dasharray'))]
                for c in clusters}
    return result


def upstream_mermaid(root, case):
    nodes = css_class(root, 'node')
    kind = case.stem
    family = 'state' if kind == 'state-diagram' else 'classId' if kind.startswith('class-diagram') else 'flowchart'

    def key(node):
        name = node.get('id').removeprefix(f'diagram-{family}-').rsplit('-', 1)[0]
        return {'root_start': '__state_start', 'root_end': '__state_end'}.get(name, name) if family == 'state' else name

    if kind == 'state-diagram':
        return {'nodes': sorted(map(key, nodes)), 'edge_count': len(css_class(root, 'transition'))}
    connectors = css_class(root, 'relation' if family == 'classId' else 'flowchart-link')
    edges = []
    for path in connectors:
        source, target = path.get('data-id').split('_')[1:3]
        edges.append([source, target, bool(path.get('marker-start')), bool(path.get('marker-end'))])
    result = topology(map(key, nodes), edges, [text(el) for el in css_class(root, 'edgeLabel')
                                              if el.tag.endswith('}g')])
    result['clusters'] = sorted(text(css_class(c, 'cluster-label')[0]) for c in css_class(root, 'cluster'))
    if case.stem == 'shapes-and-styling':
        result['distinguished_fills'] = sorted(key(node) for node in nodes
                                              if '#f96' in ET.tostring(node, encoding='unicode'))
    return result


def lambda_graph(root, case):
    family = case.parent.name
    nodes = role(root, 'node')
    rendered_edges = role(root, 'edge')
    clusters = role(root, 'cluster')
    labels = [words(attr(label, 'label')) for label in role(root, 'edge-label')]
    if case.stem == 'state-diagram':
        return {'nodes': sorted(attr(node, 'node-id') for node in nodes),
                'edge_count': len(rendered_edges)}
    name = {attr(node, 'node-id'): attr(node, 'label') for node in nodes}
    edges = []
    for edge in rendered_edges:
        source = attr(edge, 'tail-cluster') or attr(edge, 'from')
        target = attr(edge, 'head-cluster') or attr(edge, 'to')
        start, end = (attr(edge, 'marker-' + side) not in ('', 'none') for side in ('start', 'end'))
        if family == 'd2':
            edges.append(arrow(source, target, start, end))
        elif family == 'dot':
            ends = []
            for node, side in ((source, 'from'), (target, 'to')):
                port = attr(edge, side + '-port')
                compass = attr(edge, side + '-compass')
                ends.append([node, ':'.join(filter(None, (port, compass)))] if port or compass else [node])
            edges.append(sorted(ends) if case.stem == 'Heawood' else ends)
        elif family == 'structurizr':
            edges.append([[name[source]], [name[target]]])
        else:
            edges.append([source, target, start, end])
    result = topology(name.values() if family == 'structurizr' else name, edges, labels)
    cluster_names = {attr(label, 'cluster-id'): words(attr(label, 'label'))
                     for label in role(root, 'cluster-label')}
    if family == 'dot':
        result['clusters'] = sorted([cid, cluster_names[cid]] if cid in cluster_names else [cid]
                                    for cluster in clusters for cid in [attr(cluster, 'cluster-id')])
    if family == 'mermaid':
        result['clusters'] = sorted(cluster_names.values())
    if family == 'd2':
        result['node_labels'] = {key: words(label) for key, label in name.items()}
        if case.stem == 'direction-right':
            result['horizontal_order'] = [attr(n, 'node-id') for n in
                                          sorted(nodes, key=lambda n: float(attr(n, 'x')))]
    if case.stem == 'clust':
        result['cluster_paint'] = {attr(cluster, 'cluster-id'):
            [attr(cluster, 'stroke'), any(p.get('stroke-dasharray') for p in cluster.iter())]
            for cluster in clusters}
    if case.stem == 'shapes-and-styling':
        result['distinguished_fills'] = sorted(attr(n, 'node-id') for n in nodes
                                              if attr(n, 'fill') in ('#f96', '#ff9966'))
    return result


def lines_and_curves(path):
    current = None
    lengths = []
    curves = 0
    curving = False
    commands = re.findall(r'([MLCZ])([^MLCZ]*)', path)
    for command, values in commands:
        coords = list(map(float, re.findall(NUMBER, values)))
        if command == 'M':
            curving = False
        elif command == 'L' and current is not None:
            lengths.append(math.dist(current, coords[-2:]))
        elif command == 'C' and not curving:
            curving = True
            curves += 1
        if coords:
            current = coords[-2:]
    return lengths, curves


def tikz_geometry(root, upstream):
    scale = 4 / 3 if upstream else 1
    lengths = []
    curves = 0
    dash = []
    fills = 0
    strokes = set()
    for path in root.iterfind('.//{*}path'):
        stroke = path.get('stroke')
        fill = path.get('fill')
        if stroke in (None, 'none') and fill in (None, 'none'):
            continue
        pieces, count = lines_and_curves(path.get('d', ''))
        lengths += [round(scale * size, 2) for size in pieces if size > 0.05]
        curves += count
        if path.get('fill-rule') == 'evenodd':
            fills += 1
        if stroke not in (None, 'none'):
            channels = list(map(float, re.findall(NUMBER, stroke)))
            if len(channels) == 3:
                strokes.add(tuple(round(n * 2.55 if '%' in stroke else n) for n in channels))
        if path.get('stroke-dasharray'):
            data = path.get('stroke-dasharray') + ' ' + path.get('stroke-dashoffset', '0')
            dash.append([round(float(n) * scale, 2) for n in re.findall(NUMBER, data)])
    return {'line_lengths': sorted(lengths), 'curved_subpaths': curves, 'dashes': sorted(dash),
            'evenodd_paths': fills, 'stroke_colors': [list(color) for color in sorted(strokes)]}


def check_graph_geometry(root):
    for graph in role(root, 'graph'):
        limits = [float(limit) for limit in root.get('viewBox').split()][2:]
        extents = [float(attr(graph, axis)) + float(attr(graph, size))
                   for axis, size in (('x', 'width'), ('y', 'height'))]
        if any(extent > limit for extent, limit in zip(extents, limits)):
            raise ValueError('graph extends beyond the comparison SVG')
    clusters = {attr(cluster, 'cluster-id'): cluster for cluster in role(root, 'cluster')}
    for edge in role(root, 'edge'):
        for side, index in (('tail', 0), ('head', -1)):
            cluster_id = attr(edge, side + '-cluster')
            if not cluster_id:
                continue
            cluster = clusters[cluster_id]
            left, top, width, height = (float(cluster.get(key)) for key in ('x', 'y', 'width', 'height'))
            x, y = map(float, attr(edge, 'route').split()[index].split(','))
            tolerance = 0.05
            vertical = min(abs(left - x), abs(left + width - x)) <= tolerance
            horizontal = min(abs(top - y), abs(top + height - y)) <= tolerance
            if not ((vertical and top - tolerance <= y <= top + height + tolerance) or
                    (horizontal and left - tolerance <= x <= left + width + tolerance)):
                raise ValueError(f'{side} of {attr(edge, "edge-id")} does not meet {cluster_id} border')


def observations(svg, case, upstream=False):
    root = ET.parse(svg).getroot()
    family = case.parent.name
    if family == 'tikz':
        return tikz_geometry(root, upstream)
    if upstream:
        adapter = {'d2': upstream_d2, 'dot': upstream_dot, 'mermaid': upstream_mermaid,
                   'structurizr': upstream_dot}[family]
        return adapter(root, case)
    check_graph_geometry(root)
    return lambda_graph(root, case)


def agrees(key, actual, expected):
    if key in ('line_lengths', 'dashes'):
        if key == 'line_lengths':
            actual, expected = [actual], [expected]
        return len(actual) == len(expected) and all(
            len(row) == len(wanted) and all(abs(a - b) <= 0.15 for a, b in zip(row, wanted))
            for row, wanted in zip(actual, expected))
    if key == 'stroke_colors':
        # PDF percentages and RGB bytes can round gray to adjacent values.
        return len(actual) == len(expected) and all(
            all(abs(a - b) <= 1 for a, b in zip(rgb, want)) for rgb, want in zip(actual, expected))
    return actual == expected


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--lambda-exe', default=ROOT / 'lambda.exe', type=Path)
    parser.add_argument('--capture-reference', action='store_true',
                        help='rebuild the golden from the separately generated upstream SVGs')
    options = parser.parse_args()
    os.environ['TMPDIR'] = str(OUTPUT)
    fixtures = json.loads((SUITE / 'sources.json').read_text())['fixtures']
    cases = []
    for fixture in fixtures:
        source = SUITE / fixture['file']
        if hashlib.sha256(source.read_bytes()).hexdigest() != fixture['sha256']:
            raise ValueError(f'upstream fixture digest changed: {source.relative_to(SUITE)}')
        for view in fixture.get('views', [None]):
            name = source.stem + ('--' + view if view else '') + '.svg'
            cases.append((str(source.parent.relative_to(SUITE) / name), source, view))

    if options.capture_reference:
        goldens = {key: observations(OUTPUT / 'reference' / key, src, True) for key, src, _ in cases}
        GOLDEN.write_text('{\n' + ',\n'.join(
            '  ' + json.dumps(key) + ': ' + json.dumps(value, sort_keys=True, ensure_ascii=False)
            for key, value in goldens.items()) + '\n}\n')
    expected = json.loads(GOLDEN.read_text())
    if expected.keys() != {key for key, _, _ in cases}:
        raise ValueError('reference keys differ from sources.json')

    def compare(case):
        key, source, view = case
        svg = OUTPUT / 'lambda' / key
        svg.parent.mkdir(parents=True, exist_ok=True)
        # A viewport large enough for the test diagrams keeps the comparison
        # independent of continuous-export viewport clipping.
        args = [options.lambda_exe.resolve(), 'render', source, '--no-log', '-o', svg,
                '-vw', '2600', '-vh', '2300']
        if source.parent.name != 'tikz':
            args += ['--theme', 'github-light']
        if view:
            args += ['--view-key', view]
        try:
            process = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.TimeoutExpired) as error:
            return key, {'render': str(error)}
        if process.returncode != 0:
            return key, {'render': [process.returncode, process.stdout, process.stderr]}
        try:
            actual = observations(svg, source)
        except (ET.ParseError, OSError, ValueError, KeyError) as error:
            return key, {'svg': str(error)}
        differences = {name: {'lambda': actual[name], 'upstream': value}
                       for name, value in expected[key].items() if not agrees(name, actual[name], value)}
        return key, differences

    with ThreadPoolExecutor(max_workers=4) as pool:
        report = dict(pool.map(compare, cases))
    different = {key: fields for key, fields in report.items() if fields}
    (OUTPUT / 'diff.json').write_text(json.dumps(different, ensure_ascii=False, indent=2) + '\n')
    for key, fields in different.items():
        print(f'{key}: {", ".join(fields)}')
    print(f'{len(cases) - len(different)}/{len(cases)} renders match the checked upstream observations')
    return 1 if different else 0


if __name__ == '__main__':
    sys.exit(main())
