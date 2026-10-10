"""Check completeness and link integrity of the AntD feature disposition."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
catalog = json.loads((Path(__file__).parent / 'catalog.manifest').read_text())
entries = catalog['components']
assert len(entries) == 73 and len({entry['name'] for entry in entries}) == 73
lock = json.loads((ROOT / catalog['upstream']['reference_app'] / 'package-lock.json').read_text())
upstream = lock['packages']['node_modules/antd']
assert upstream['version'] == catalog['upstream']['version']
assert upstream['integrity'] == catalog['upstream']['integrity']
assert re.fullmatch(r'[0-9a-f]{40}', catalog['upstream']['commit'])
exports = set(re.findall(r'^pub fn (\w+)\(', (ROOT / 'lmd/package/ui/dtna.ls').read_text(), re.M))
for entry in entries:
    assert entry['required_features'] and entry['status'] in ('planned', 'partial', 'blocked', 'complete')
    assert entry['owner'].startswith('lmd/package/ui/'), entry['name']
    assert re.search(r'M[0-7]', entry['milestone']), entry['name']
    if entry['status'] != 'complete':
        assert entry['remaining'], entry['name']
    else:
        assert not entry['remaining'], entry['name']
    if entry['status'] in ('partial', 'complete'):
        assert entry['implemented'] and entry['exports'] and entry['evidence'], entry['name']
        assert (ROOT / entry['owner']).is_file(), entry['owner']
        assert set(entry['exports']) <= exports, entry['name']
        for fixture in entry['evidence']:
            assert (ROOT / fixture).is_file(), fixture
    else:
        assert entry.get('owner_status') or (ROOT / entry['owner']).is_file(), entry['name']
    if entry['status'] == 'complete':
        assert any(fixture.startswith('test/lambda/ui_dtna/') for fixture in entry['evidence']), entry['name']
        assert any(fixture.startswith('test/ui/dtna/') and fixture.endswith('.json') for fixture in entry['evidence']), entry['name']
for script in (ROOT / 'test/lambda/ui_dtna').glob('*.ls'):
    assert script.with_suffix('.txt').is_file(), script
print(f"dtna catalog: {len(entries)} entries; " + ', '.join(
    f"{sum(entry['status'] == status for entry in entries)} {status}"
    for status in ('complete', 'partial', 'planned', 'blocked')))
