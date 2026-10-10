"""Compare Spin static geometry, busy content and real delay/fullscreen observations."""
from copy import deepcopy
from reference_oracles import load_case, check_rect, check_style

capture, fixture = load_case('spin-modes', 'spin_modes')
checked = 0
for event in fixture['events']:
    if event['type'] == 'click':
        break
    if event['type'] not in ('assert_rect', 'assert_style'):
        continue
    oracle = deepcopy(event)
    oracle['target']['selector'] = oracle['target']['selector'].replace(' .dtna-spin-', '-')
    (check_rect if event['type'] == 'assert_rect' else check_style)(oracle, capture)
    checked += 1
assert checked == 10
assert capture['interaction'] == {
    'count': '1', 'removed': True, 'fullscreenHidden': True, 'blocked': True,
    'fullscreen': {'x': 0, 'y': 0, 'width': 600, 'height': 800},
}
assert capture['animationPolicy'].endswith('this PNG has no pixel-parity gate')
print(f'dtna Spin reference: {checked} geometry/style oracles and delay, blocked-content, removal and fullscreen observations verified')
