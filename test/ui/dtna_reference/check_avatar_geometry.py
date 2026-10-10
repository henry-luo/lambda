"""Compare Avatar size, corner and typography oracles at every live viewport."""
from reference_oracles import load_case, check_responsive_oracles

capture, fixture = load_case('avatar-modes', 'avatar_modes')
checked = check_responsive_oracles(capture, fixture)
assert checked == 23
print(f'dtna Avatar reference: {checked} native size, corner and typography oracles agree across six viewport states')
