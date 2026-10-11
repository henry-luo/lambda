"""Compare Segmented geometry and pinned browser interaction observations."""
from reference_oracles import load_case, check_rect

capture, fixture = load_case('segmented-modes', 'segmented_modes')
checked = 0
for event in fixture['events']:
    if event['type'] == 'assert_rect':
        check_rect(event, capture)
        checked += 1
assert checked == 14
assert capture['interaction'] == {
    'count': '4', 'request': 'seg-named:3', 'entries': 'choice:on',
    'controlledLabel': 'One', 'controlledNativeValue': 'on',
}
print(f'dtna Segmented reference: {checked} native geometry oracles agree; upstream radio form-value difference recorded')
