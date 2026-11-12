"""Reuse exact-output/deadline checks, with connectivity-owned evidence and GC cases."""
import importlib.util
import fcntl
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scale_goldens", ROOT / "test/sync_scale/test_goldens.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
gate.AREA = ROOT / "temp/sync-connectivity" / ("gc" if "--gc" in sys.argv else "goldens") / gate.TIER
gate.GC = ["test/lambda/proc/sync_rtc.ls", "test/lambda/proc/sync_rendezvous.ls",
           "test/lambda/proc/sync_connectivity_protocol.ls", "test/lambda/proc/sync_stream_cancel.ls",
           "test/lambda/proc/sync_pairing.ls", "test/lambda/proc/sync_agent_recovery.ls"]

if __name__ == "__main__":
    # Existing repository goldens use shared fixed fixture paths across tiers.
    lock = ROOT / "temp/sync-connectivity/golden.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    with lock.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        gate.main()
