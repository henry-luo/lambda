"""Check completeness and link integrity of the AntD feature disposition."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
catalog = json.loads((Path(__file__).parent / 'catalog.manifest').read_text())
entries = catalog['components']
assert len(entries) == 73 and len({entry['name'] for entry in entries}) == 73
exports = set(re.findall(r'^pub fn (\w+)\(', (ROOT / 'lmd/package/ui/dtna.ls').read_text(), re.M))
for entry in entries:
    assert entry['required_features'] and entry['status'] in ('planned', 'partial', 'blocked', 'complete')
    if entry['status'] != 'complete':
        assert entry['remaining'], entry['name']
    if entry['status'] == 'partial':
        assert entry['implemented'] and entry['exports'] and entry['evidence'], entry['name']
        assert (ROOT / entry['owner']).is_file(), entry['owner']
        assert set(entry['exports']) <= exports, entry['name']
        for fixture in entry['evidence']:
            assert (ROOT / fixture).is_file(), fixture
for script in (ROOT / 'test/lambda/ui_dtna').glob('*.ls'):
    assert script.with_suffix('.txt').is_file(), script
print(f"dtna catalog: {len(entries)} entries; " + ', '.join(
    f"{sum(entry['status'] == status for entry in entries)} {status}"
    for status in ('complete', 'partial', 'planned', 'blocked')))
