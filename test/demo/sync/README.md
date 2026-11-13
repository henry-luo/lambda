# Lambda Sync demo

Run on two Macs on the same local network:

```sh
./lambda.exe demo sync
```

1. On the first device, enter your user ID and choose **Create sync**.
2. On the other device, enter the same user ID, choose **Link this device**,
   and enter the one-time code displayed on the first device.
3. Add or edit files in **`~/lambda-sync`**. They sync in both directions.

The app creates the folder if needed. Its **Open folder** button opens it in
Finder. Keep both windows open for synchronization. Once paired, reopening
`lambda demo sync` remembers the user and devices and reconnects automatically.
**Pause** stops sync; **Resume** restarts it. **Link another device** opens a fresh
pairing window. The code expires after two minutes and admits one device.

Use different files for an initial offline test: close both windows, add a file
on each Mac, reopen both windows and confirm both files arrive on both devices.
External concurrent edits to the same file can require explicit conflict review.
Existing files are captured into version history; the demo uses the real sync
repository, rather than a disposable sample tree.

If discovery fails, allow local-network/incoming connections for Lambda and
check that the network permits devices to reach one another. No cloud function,
S3 bucket, STUN service or WebRTC dependency preparation is needed for this LAN
profile. Initial support is the native macOS host.

The agent scans on startup to capture changes made while it was stopped, then
uses OS directory notifications. It batches changes for about 50 ms, with a
250 ms maximum batch wait, and starts
delivery to online peers. Connections are reused for changes in either direction
and close after ten quiet seconds. Startup/rediscovery exchanges catch up
without a new filesystem event. Recovery and occasional verification retain full
scans; receiving files also performs the captures needed for safe installation.

## Identity and storage

The public user ID selects discovery candidates; it does not prove account
ownership. A one-time code authorizes the initial device link (D11.2.3).
User IDs are trimmed and otherwise compared exactly. A production account
creation/sign-in flow can replace this bootstrap and enroll devices under an
authenticated account. Matching a public name alone never grants file access.

Each device has independent signing/exchange keys and an encrypted local
repository. The wrapping key is stored through `lambda.io.secret` in the login
Keychain; the OS may ask to allow access. Public profile metadata and encrypted
agent files live under `~/Library/Application Support/Lambda Sync Demo`, outside
the working folder. Pairing codes are not saved. The demo retains one active
user/folder per profile; it does not silently change an existing folder's account.
One running window owns a profile's sync operations.

Discovery, pairing, profile lifecycle, synchronization and UI are Lambda Script
(S12.1.1v3, D9.3.4). The C++ catalog only registers the demo. Existing native
Bonjour, WebSocket, directory monitoring, crypto and OS secret APIs supply IO (D11.1.2).
Document-owned tasks cancel listeners, advertisements and replication on close
(S12.4.4v4, S13.1.2v2). A pairing window pauses ordinary synchronization, then
resumes it after enrollment. Already paired devices need no new code on restart.

The temporary code has 80 random bits, shown in five groups of four hex digits.
The pairing service advertises an account-hash prefix and device suffix, never
the code. Role-bound AEAD proofs bind the full account digest, both signed device
descriptors and fresh nonces. Native authentication checks the proofs; existing
signed, recipient-encrypted invitations provision the space secret and local
grants. Each window has a two-minute absolute deadline, eight candidate attempts,
16 KiB binary frames and at most five seconds per receive. The host spends its
code on the first valid client proof, even if the subsequent receipt is lost.
A new window always creates a new code. This reuses the experimental
[peer security profile](../../../lmd/package/sync/PEER.md), not an account service.

Ordinary replication resumes an unfinished installation before scanning on
restart. An externally changed, blocked partial install preserves its capture
and reports that review is needed. Automatic rebase/resolution of that existing
repository state remains outside this demo. See the
[local repository limits](../../../lmd/package/sync/LOCAL_REPOSITORY.md).

## Development and verification

`SYNC_DEMO_ROOT` overrides private profile storage; `SYNC_DEMO_DIR` overrides the
working folder. Use absolute, separate paths when isolating tests. Defaults
remain unchanged for normal launches. Tests supply wrapping keys directly to
the controller and do not create test entries in the user's Keychain.

```sh
python3 test/demo/sync/test_sync.py
LAMBDA_EXEC_BACKEND=interp python3 test/demo/sync/test_sync.py
LAMBDA_EXEC_BACKEND=jit python3 test/demo/sync/test_sync.py
python3 test/sync_connectivity/test_goldens.py
python3 test/sync_connectivity/test_goldens.py --gc
python3 test/sync_connectivity/test_lan_events.py
./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.SyncDemoFirstRunAndPairingForm'
```

The process gate uses real Bonjour/listeners with separate device profiles and
folders. It covers automatic enrollment, bidirectional external changes, restart,
offline moves, spent-code rejection and adding another device. The UI gate
checks first-run navigation and validation with forced/poisoned GC, without
opening the Keychain. `sync_pairing.ls` pins account/code/transcript/role/expiry
rejections. `sync_agent_recovery.ls` pins unfinished and raced-install behavior.
These local checks do not qualify two physical machines or cross-NAT operation.

Implementation and evidence: [completed implementation record](<../../../vibe/impl/Lambda_Impl_Sync_Demo (done).md>).

Event monitoring and short connection bursts: [implementation and qualification](<../../../vibe/impl/Lambda_Impl_Sync_LAN_Events (done).md>).
