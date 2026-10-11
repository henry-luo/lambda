"""Check Rate sizing and committed interaction states against pinned AntD."""
from reference_oracles import load_case, check_rect, check_style

capture, fixture = load_case('rate-modes', 'rate_modes')
checked = 0
for event in fixture['events']:
    if event['type'] in ('assert_rect', 'assert_style'):
        (check_rect if event['type'] == 'assert_rect' else check_style)(event, capture)
        checked += 1
assert checked == 22
assert capture['interaction'] == {
    'count': '13', 'request': 'rate-custom:3',
    'values': {'rate-default': 0, 'rate-half': 0, 'rate-fixed': 2,
               'rate-disabled': 3, 'rate-readonly': 3, 'rate-no-clear': 2,
               'rate-no-keyboard': 3, 'rate-small': 1, 'rate-large': 4,
               'rate-custom': 3, 'rate-rtl': 1.5},
}
print(f'dtna Rate reference: {checked} geometry/font oracles and committed interactions agree')
