"""Run native snapshot assertions against verified, generated AntD captures."""
import json
import os
import subprocess
from reference_oracles import ROOT, load_case

# bounds are mismatch percentages at the native harness's fixed YIQ threshold.
CASES = {'avatar-modes': ('avatar_modes', 0.1)}
output = ROOT / 'temp/ui_dtna/pixel_checks'
output.mkdir(parents=True, exist_ok=True)
for reference_name, (native_name, threshold) in CASES.items():
    capture, fixture = load_case(reference_name, native_name)
    assert capture['viewport'] == {**fixture['viewport'], 'scale': 1}
    fixture['events'].append({
        'type': 'assert_snapshot',
        'reference': f'temp/ui_dtna/reference/{capture["filename"]}',
        'threshold': threshold,
        'save_actual': str(output / f'{native_name}_actual.png'),
        'save_diff': str(output / f'{native_name}_diff.png'),
    })
    event_file = output / f'{native_name}.json'
    event_file.write_text(json.dumps(fixture, indent=2) + '\n')
    for tier in ('interp', 'jit'):
        logfile = output / f'{native_name}_{tier}.log'
        with logfile.open('w') as stream:
            result = subprocess.run([
                str(ROOT / 'lambda.exe'), 'view', fixture['html'],
                '--event-file', str(event_file), '--headless',
                '--font-dir', 'test/layout/data/font', '--no-log',
            ], cwd=ROOT, env={**os.environ, 'LAMBDA_EXEC_BACKEND': tier},
                stdout=stream, stderr=subprocess.STDOUT, timeout=120)
        text = logfile.read_text(errors='replace')
        assert result.returncode == 0, f'{native_name}/{tier}: {logfile}'
        assert '27 passed, 0 failed' in text, f'assertion count changed: {logfile}'
        print(f'dtna pixels: {native_name}/{tier} passed at {threshold}% mismatch bound')
