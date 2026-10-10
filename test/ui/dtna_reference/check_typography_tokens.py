"""Compare scoped typography and edit assertions with the pinned AntD capture."""
from reference_oracles import load_case, geometry, check_rect, check_style

capture, fixture = load_case('typography-tokens', 'typography_tokens')
states = iter(capture['edits'])
state = capture
checked = 0
for event in fixture['events']:
    if event['type'] == 'type' or (event['type'] == 'key_press' and event['key'] == 'backspace'):
        state = next(states)
    elif event['type'].startswith('assert_'):
        selector = event['target']['selector']
        assert selector.startswith('#'), selector
        identifier = selector[1:]
        nodes = geometry(state)
        assert identifier in nodes, identifier
        actual = nodes[identifier]
        if event['type'] == 'assert_style':
            check_style(event, state)
        elif event['type'] == 'assert_rect':
            check_rect(event, state)
        elif event['type'] == 'assert_value':
            assert state['value'] == event['equals'], (event, state['value'])
        elif event['type'] == 'assert_focus':
            assert state['focus'] == identifier, (event, state['focus'])
        elif event['type'] == 'assert_caret':
            assert state['caret'] == event['char_offset'], (event, state['caret'])
        else:
            raise AssertionError(event['type'])
        checked += 1
assert checked == 30
assert next(states, None) is None
print(f'dtna typography reference: {checked} native font, line-box and edit assertions match AntD 6.6.5')
