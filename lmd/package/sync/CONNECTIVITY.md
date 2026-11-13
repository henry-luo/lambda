# Automatic native peer connectivity

The native connectivity agent combines private Bonjour hints, authenticated
WebRTC DataChannels, finite HTTP signaling and periodic cloud synchronization.
It reuses the existing repository, CRDT, enrollment and application handshake.
Discovery grants no access. Policy remains Lambda Script (S12.1.1v3, D9.3.4);
native mechanisms use the central IO boundary (D11.1.2, D11.2.1–D11.2.4).

For a simple LAN trial, use [`lambda demo sync`](../../../test/demo/sync/README.md).
It creates `~/lambda-sync`, pairs devices with a user ID and one-time code, and
remembers the pairing for later launches.

## Prepare and run

On the initial macOS profile, prepare the optional static WebRTC dependency:

```sh
python3 utils/prepare_sync_connectivity.py
make build
```

The recipe checks libdatachannel v0.24.6 at commit
`6b1e2e620f1e37f0eafeee702eaea0043cb305fd` and its pinned submodules, builds them
unmodified, and installs into `build/deps/connectivity/`. It disables media,
WebSockets, examples and dependency tests. `provenance.json` records the inputs.
No sibling sources or vendor files are patched. Without the prepared prefix,
the native capability report has `rtc:false`; acquisition returns an explicit
unavailable error. The initial Bonjour implementation uses macOS DNS-SD.

Create/enroll devices using the [peer CLI](PEER.md). Then give each native host
its own public request file and protected wrapping key, as with existing agents:

```json
{"action":"run","directory":"/private/device-agent",
 "cloud":{"url":"https://your-function.example/sync","authority":"OWNER_DEVICE_ID"},
 "options":{"port":0,"bind":"0.0.0.0","mdns":true,"rtc":true,
   "stun":["stun:your-stun-service.example:3478"],
   "status":"/private/device-agent/connectivity.json"}}
```

```sh
LAMBDA_SYNC_REQUEST="$PWD/temp/connectivity-request.json" \
LAMBDA_SYNC_KEY="$DEVICE_WRAPPING_KEY" \
./lambda.exe run lmd/package/sync/connectivity_cli.ls
```

`cloud:null` permits automatic LAN discovery and direct synchronization without
an HTTP endpoint. `stun:[]` uses host candidates only. STUN is a separate UDP
facility; an ordinary HTTP function does not provide it. Direct paths that fail
use the existing periodic HTTP/object-store path. TURN is not required by this
profile. Neither signaling nor document fallback keeps a cloud WebSocket open.

CLI actions are `capabilities`, `discover` (optional `timeout_ms`, default 1000),
and `run`. Programmatic entry points are `connectivity_agent.run(directory,key,
config=null,settings={},observer=null)` and the discovery/signaling helpers in `connectivity`.

## Schedule and status

With `config:null` and `mdns:true`, the native LAN agent uses OS notifications
and short connection bursts (S12.1.1v3, D9.3.4). It registers the watcher before
the startup scan, exchanges changes when a peer first appears, batches later
notifications and reconciles affected subtrees. Either device can connect.
Already authenticated connections carry further rounds in either direction and
close after the idle timeout. Repository callbacks have one script owner;
network waits hold no repository transaction (S12.4.4v4, S13.1.2v2, S13.3.1v2).
Native monitoring uses libuv's OS event backend, not `uv_fs_poll`.

LAN defaults add `debounce_ms:50`, `batch_ms:250`, `idle_ms:10000`,
`burst_ms:60000`, `verify_ms:60000` and a 100 ms scheduling/idle-message tick.
`interval_ms` can override that tick, capped at `idle_ms`. Scans run for startup,
restart, missed/overflowed notifications and occasional verification. Atomic
installation still captures the tree when needed to preserve external changes.
Neither a tick nor an unchanged discovery result triggers a scan or connection.
Discovery refreshes every ten seconds; a changed/reappearing address requests
catch-up. Verification also requests anti-entropy. Failed delivery retries after
`retry_ms`; committed local changes remain in the repository until acknowledged.

LAN status adds `monitor:'os'`, full/incremental scan counts, watcher-event counts,
connection/round counts, active connections, pending batches and peer availability.
`captured_frontier` identifies committed source capture; `installed_frontier`
identifies completed destination installation. File visibility can precede the
installation journal's final commit.
Installation/review state is distinct from network delivery. A failed native
monitor surfaces an error instead of silently disabling change detection.
The initial qualification host remains macOS; Linux maintains individual
directory watches, and other-OS qualification remains pending.

The cloud/ICE profile retains the following periodic schedule:

| Setting | Default | Bound |
|---|---:|---|
| `port`, `bind` | 0, `0.0.0.0` | Ephemeral or explicit listener; native IPv4 binding |
| `mdns`, `rtc` | true | Enable discovery and WebRTC independently |
| `stun` | empty | Up to four `stun:` URLs, each at most 512 bytes |
| `establish_ms` | 15,000 | 1,000–60,000 per ICE attempt |
| `retry_ms` | 5,000 | 1,000–3,600,000; failures back off to at most 60 seconds |
| `interval_ms` | 1,000 | 100–60,000; local accept/active-attempt tick |
| `cloud_ms` | 30,000 | 1,000–3,600,000 between periodic cloud rounds |
| `rounds`, `stop_after` | 0 | Zero runs continuously; positive bounds up to 1,000,000 |
| `status`, `ready` | null | Optional atomic JSON status and initial listener-port files |

For cloud/ICE negotiation, the smaller device ID initiates; the other answers. The agent
rotates peers and finite inbox pages. It attempts discovered LAN paths or ICE
before the next bulk cloud round. A failed attempt schedules fallback; cloud
publication also runs periodically after successful direct delivery, preserving
long-term storage. A direct exchange is not a cloud persistence acknowledgment.
One script owner serializes scans, materialization, peer work and cloud work.

Presence refreshes every 20 seconds; an idle receiving agent checks signaling
at most once per second, in pages of at most eight members. Outstanding negotiation
uses finite requests with two-second client timeouts. The attempt deadline is
checked across gathering and answer/connection waits; in-flight HTTP and local
IO have their own bounds. It is not a hard real-time scheduling guarantee.

An optional `observer(status)` procedure receives the same public progress as
the status file, allowing a document-owned UI without polling files
(S12.4.4v4, S13.1.2v2). On startup the agent resumes any unfinished installation
before scanning. A blocked installation preserves its capture and requires review.

Status reports `phase`, `connected`, last selected `transport`, `peer`, attempts,
successful sessions, retries, fallback reason, error code, cloud coverage,
capabilities and initial advertisement status. `connected:true` describes an
active authentication/exchange, while idle `transport:'peer'` records the last
successful delivery method. Object/request progress is reported during RTC
exchange; completed exchanges include their synchronization summary. Bonjour
registration errors appear separately in `discovery_error`. Status files contain
no private keys, SDP or ICE credentials. The cloud/ICE agent uses finite
synchronization sessions; the native LAN agent reuses a channel during a bounded
burst. Its `connected:true` includes the short warm idle period.

## Signaling and trust

The existing signed cloud control authenticates three finite service operations:
`rendezvous_current`, `rendezvous_put`, `rendezvous_get` (the latter uses explicit
member IDs; there is no public listing). Presence, offers and answers occupy fixed
per-author/per-recipient slots. Replacement requires the exact opaque ETag and
the next signed sequence. Signed records are at most 120 KiB; an eight-record
reply fits the existing 1 MiB response limit. An identical retry is idempotent. Expired slots retain
their sequence fence; expiration does not delete their current object.

Records sign version, space, author, recipient, kind, sequence, timestamps,
session and encrypted payload. These routing fields also bind AEAD encryption
under the existing space key. TTL is 60 seconds by default, at most 120 seconds;
future issue-time skew is at most five seconds. Clients validate signatures,
membership, expiry and recipient before using the decrypted SDP. Responses cannot
substitute an unrequested author. Presence remains advisory.

A new process chooses a random instance ID. An offer names both process instances;
its answer names those instances, the exact offer-body digest and the random
attempt ID. Stale answers/offers cannot enter a new instance's connection. The
application's signed ephemeral handshake separately pins the enrolled peer and
derives ordered encrypted record keys. Current-member checks continue during
transfer. Previously granted group keys/data are not erased by revocation. Native peer
trust remains the existing explicit local grant list; a cloud roster change
does not silently replace those grants. Withdraw direct access through local
peer revocation as well.

Bonjour advertises `_lambda-sync._tcp.local.` using a private space/key/device
digest as its service name. It carries no membership grant. Native browse/resolve
returns at most 64 hints within a finite budget; scripts filter expected names
and DNS host syntax before trying an address. Registration forbids auto-renaming
and confirms the platform callback before reporting readiness.
`discovery.browse(timeout, {names:[...]})` filters expected instances before
resolution; pairing uses `{prefix:...}`. A candidate receives a share of the
remaining resolution deadline, so an unresolved unrelated service cannot starve
enrolled peers. The original one-argument API remains available. Filters are
discovery hints and never replace authentication (D9.3.4, D11.1.2).

At most 64 historical members imply at most 8,128 logical signaling slots, created
only when used. Native message size is 256 KiB; received messages are bounded by
2 MiB and 64 queued messages. Backpressure and operation deadlines bound native
waits. Native status byte counters count accepted binary DataChannel message bytes,
excluding UDP/DTLS/SCTP overhead. Task completion/cancellation joins callbacks and
releases channels and advertisements (S12.4.4v4, S13.1.2v2). No script objects are
allocated or accessed by library worker callbacks; precise roots own carriers.

Provider object versioning can retain older slot copies independently of these
logical bounds. A lifecycle rule may remove noncurrent slot versions; it must
preserve the current sequence fence. Signaling slots do not enter saved history,
repository receipts or content garbage collection.

## Verification

```sh
python3 test/sync_connectivity/test_goldens.py
python3 test/sync_connectivity/test_goldens.py --gc
python3 test/sync_connectivity/test_connectivity.py
python3 test/sync_connectivity/test_lan_events.py
python3 test/sync_connectivity/test_stun.py
LAMBDA_CLOUD_TEST_STORAGE_FILE="$PWD/temp/private-storage.json" \
python3 test/sync_connectivity/test_signaling.py
```

Select `LAMBDA_EXEC_BACKEND=auto|interp|jit` and optionally `LAMBDA_SYNC_EXE`.
The golden runner serializes fixed shared fixture directories across tiers.
The process gates own/reap client/function children; an operator owns the isolated
S3 backend. Normal goldens keep 60 seconds; focused GC/poison keeps 300 seconds.
Use a release executable for measurements. Local independent-process checks
do not establish cross-NAT, physical-device, browser, hosted-provider or other-OS
qualification. See the [implementation record](<../../../vibe/impl/Lambda_Impl_Sync_Connectivity (done).md>).

Protocols: [ICE](https://www.rfc-editor.org/rfc/rfc8445),
[DataChannels](https://www.rfc-editor.org/rfc/rfc8831),
[libdatachannel C API](https://libdatachannel.org/pages/reference.html).
