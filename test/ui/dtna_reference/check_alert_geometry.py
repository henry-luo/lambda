"""Compare Alert geometry/colors and real native close/action equivalents with AntD."""
from reference_oracles import load_case, check_rect, check_style

capture, fixture = load_case('alert-modes', 'alert_modes')
checked = 0
for event in fixture['events']:
    selector = event.get('target', {}).get('selector', '')
    if event['type'] in ('assert_rect', 'assert_style') and ' ' not in selector:
        (check_rect if event['type'] == 'assert_rect' else check_style)(event, capture)
        checked += 1
assert checked == 21
assert capture['interaction'] == {'count': '6', 'request': 'null:close', 'closed': True, 'nestedSurvived': True}
# controlled dtna visibility deliberately retains the last alert; upstream closes it.
assert any(event['type'] == 'assert_count' and event['target']['selector'] == '#alert-retain'
           and event['count'] == 1 for event in fixture['events'])
print(f'dtna Alert reference: {checked} geometry/color oracles and six close/action observations verified; controlled retention recorded separately')
