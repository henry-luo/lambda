"""Compare Button geometry/appearance, semantic slots and real native actions with AntD."""
from reference_oracles import load_case, check_rect, check_style

capture, fixture = load_case('button-modes', 'button_modes')
checked = 0
for event in fixture['events']:
    if event['type'] == 'click':
        break
    if event['type'] not in ('assert_rect', 'assert_style'):
        continue
    if ' ' in event['target']['selector']:
        continue
    (check_rect if event['type'] == 'assert_rect' else check_style)(event, capture)
    checked += 1
assert checked == 34
assert capture['semantic'] == {'root': True, 'icon': True, 'contentWeight': '700'}
assert capture['interaction'] == {
    'count': '12', 'request': 'button-controlled', 'submits': '1',
    'removed': True, 'reset': 'seed',
}
print(f'dtna Button reference: {checked} geometry/style oracles, semantic slots and keyboard/loading/form/removal observations verified')
