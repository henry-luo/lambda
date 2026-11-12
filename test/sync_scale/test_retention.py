"""Run the existing retention/cache/crash contract against paged cloud storage."""
from pathlib import Path
import os,sys
ROOT=Path(__file__).resolve().parents[2]
import importlib.util
spec=importlib.util.spec_from_file_location('retention_gate',ROOT/'test/sync_retention/test_retention.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
os.environ.setdefault('LAMBDA_CLOUD_TEST_STORAGE_PROTOCOL','catalog-2')
gate.AREA=ROOT/'temp/sync-recovery-scale'/('retention-'+gate.peer.TIER)
gate.peer.AREA=gate.cloud.AREA=gate.docs.AREA=gate.AREA
if __name__=='__main__':
    try:gate.main()
    finally:
        for proxy in gate.proxies:
            proxy.shutdown();proxy.server_close()
        for child in gate.peer.children:
            if child.poll() is None:child.kill();child.wait()
