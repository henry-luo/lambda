"""Check the native Grid fixture's exact geometry against the pinned AntD capture."""
from reference_oracles import load_case, check_responsive_oracles

capture, fixture = load_case('grid-modes', 'grid_modes')
checked = check_responsive_oracles(capture, fixture)
assert checked == 32
print(f'dtna Grid reference: {checked} native geometry assertions match AntD 6.6.5 within 0.2px')
