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
           "test/lambda/proc/sync_pairing.ls", "test/lambda/proc/sync_agent_recovery.ls",
           "test/lambda/proc/sync_disk_watch.ls", "test/lambda/proc/sync_event_capture.ls",
           "test/lambda/proc/sync_install_additions.ls",
           "test/lambda/proc/sync_lan_schedule.ls", "test/lambda/proc/sync_discovery_filter.ls"]
# The full burst workflow has real 10-second/500 ms scheduling bounds. Allocation-
# forced GC exceeds those bounds; retain it in ordinary exact/process acceptance.
# Stress evidence and the separate qualification limit are recorded as SD-05 in
# vibe/impl/Lambda_Impl_Sync_LAN_Events (done).md; native watcher lifetimes remain above.

if __name__ == "__main__":
    # Existing repository goldens use shared fixed fixture paths across tiers.
    lock = ROOT / "temp/sync-connectivity/golden.lock"
    lock.parent.mkdir(parents=True, exist_ok=True)
    with lock.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        gate.main()
