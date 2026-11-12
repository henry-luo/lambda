"""Reuse the exact-golden GC runner with the retention storage boundary included."""
from pathlib import Path
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/sync_documents'))
import test_stress as gate

gate.AREA = ROOT / 'temp/sync-retention' / ('stress-' + os.environ.get('LAMBDA_EXEC_BACKEND', 'auto'))
gate.FIXTURES = ['test/lambda/proc/sync_retention_gc.ls', *gate.FIXTURES]

if __name__ == '__main__':
    gate.main()
