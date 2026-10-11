"""Compare Tag geometry/colors and real selection/close behavior with AntD."""
from reference_oracles import load_case, check_rect, check_style

capture, fixture = load_case('tag-modes', 'tag_modes')
checked = 0
for event in fixture['events']:
    if event['type'] in ('assert_rect', 'assert_style'):
        (check_rect if event['type'] == 'assert_rect' else check_style)(event, capture)
        checked += 1
assert checked == 16
assert capture['interaction'] == {
    'count': '5', 'request': 'tag-close:close', 'checked': 'false',
    'fixed': 'false', 'retained': True, 'closed': True,
}
print(f'dtna Tag reference: {checked} native geometry/color oracles and selection/close observations agree')
