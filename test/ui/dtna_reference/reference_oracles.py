"""Shared provenance loading and rectangle comparisons for native fixture oracles."""
import json
from hashlib import sha256
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def load_provenance():
    provenance = json.loads((ROOT / 'temp/ui_dtna/reference/provenance.json').read_text())
    # reject captures produced from stale reference sources or different font bytes.
    for name, expected in provenance['inputs'].items():
        source = ROOT / 'test/ui/dtna_reference/app' / name
        assert sha256(source.read_bytes()).hexdigest() == expected, f'stale capture input: {name}'
    for face in provenance['faces']:
        assert sha256(Path(face['path']).read_bytes()).hexdigest() == face['sha256'], face['path']
    return provenance


def load_case(reference_name, native_name):
    provenance = load_provenance()
    capture = next(case for case in provenance['captures'] if case['fixture'] == reference_name)
    png = ROOT / 'temp/ui_dtna/reference' / capture['filename']
    assert sha256(png.read_bytes()).hexdigest() == capture['sha256'], f'changed capture: {png}'
    fixture = json.loads((ROOT / f'test/ui/dtna/{native_name}.json').read_text())
    return capture, fixture


def geometry(state):
    return {node['id']: node for node in state['geometry'] if node['id']}


def check_rect(event, state):
    nodes = geometry(state)
    selector = event['target']['selector']
    assert selector.startswith('#') and selector[1:] in nodes, selector
    actual = nodes[selector[1:]]
    relative = nodes[event['relative_to'][1:]] if 'relative_to' in event else None
    for axis in ('x', 'y', 'width', 'height'):
        if axis in event:
            value = actual[axis] - (relative[axis] if relative and axis in ('x', 'y') else 0)
            assert abs(value - event[axis]) <= event['tolerance'], (selector, axis, value, event[axis])


def check_style(event, state):
    selector = event['target']['selector']
    assert selector.startswith('#'), selector
    actual = geometry(state)[selector[1:]]['style'][event['property']]
    assert actual == event['equals'], (selector, event['property'], actual, event['equals'])


def check_responsive_oracles(capture, fixture):
    states = iter([capture, *capture['resizes']])
    state = next(states)
    checked = 0
    for event in fixture['events']:
        if event['type'] == 'resize':
            state = next(states)
            assert state['viewport']['width'] == event['width']
            assert state['viewport']['height'] == event['height']
        elif event['type'] in ('assert_rect', 'assert_style'):
            (check_rect if event['type'] == 'assert_rect' else check_style)(event, state)
            checked += 1
    assert next(states, None) is None
    return checked
