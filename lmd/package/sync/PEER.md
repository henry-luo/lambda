# Authenticated native peer synchronization

The native LAN profile connects independent local repositories over real sockets.
It transfers missing encrypted objects and merges signed workspace checkpoints,
then materializes files through the existing installation journal. Devices retain
their own SQLite databases, accepted disk baselines, actor identities, snapshots
and offline changes. A connected peer does not require Internet access. The [cloud adapter](CLOUD.md)
adds long-term object storage and periodic HTTP fallback around this transport.
The [connectivity agent](CONNECTIVITY.md) adds automatic private LAN discovery,
finite signed signaling and a native WebRTC DataChannel carrier for this same
application protocol. Its qualification is separate from physical cross-NAT use.

Policy, pairing, session framing, transfer scheduling and orchestration are Lambda
Script (S12.1.1v3, D9.3.4). C++ supplies raw public-key operations and the existing
task-owned WebSocket transport (D11.1.2, S13.1.2v2). Bound security clients cannot
acquire the new unbrokered listener; this is a trusted native CLI host profile
under D11.2.1–D11.2.4.

## Run the agent

Each device needs an existing working folder and a separate private agent
directory outside that folder. Use a different random 32-byte wrapping key on
each device. Keep that key in the trusted host's secret storage: it unlocks the
encrypted credential file and is not a password. OS-vault integration is separate.

The CLI reads a JSON request from `LAMBDA_SYNC_REQUEST`, and the wrapping key as
64 hexadecimal digits from `LAMBDA_SYNC_KEY`:

```sh
LAMBDA_SYNC_REQUEST=./temp/alice-request.json \
LAMBDA_SYNC_KEY="$ALICE_SYNC_WRAPPING_KEY" \
./lambda.exe run lmd/package/sync/peer_cli.ls
```

For example, first create a device with:

```json
{"action":"device","directory":"/Users/alice/.lambda-sync/family"}
```

Then initialize its space:

```json
{"action":"init","directory":"/Users/alice/.lambda-sync/family",
 "root":"/Users/alice/Family","space":"family","label":"Alice"}
```

On the other device, run `device`, then `export_identity`. Transfer the public
descriptor to Alice and verify Bob's displayed fingerprint through a trusted
channel. Alice runs `invite` using that descriptor. Transfer the resulting
encrypted invitation to Bob; Bob runs `enroll` with Alice's verified fingerprint.
The invitation binds its sender, intended recipient and space secret. Each side
then authorizes the other as a full-space writer and checkpoint authority.

All requests require `directory`; the remaining fields are:

| Action | Fields and result |
|---|---|
| `device` | Create encrypted device credentials; return public descriptor/fingerprint. |
| `init` | `root`, `space`, `label`; initialize a new local repository. |
| `identity` | Return this device's public descriptor. |
| `export_identity` | `output`; write the typed public descriptor for pairing. |
| `invite` | `recipient` descriptor filename, `output` invitation filename; authorize the intended device locally. |
| `enroll` | `root`, `label`, `invitation` filename, `expected` inviter fingerprint; initialize a new repository. |
| `serve` | `port`, `bind`, optional `ready` port-file and `max_sessions`; zero sessions means run continuously. |
| `sync` | `url`, `peer` expected fingerprint; exchange both directions once and attempt installation. |
| `watch` | `url`, `peer`, optional `interval_ms` (default 5000) and `rounds` (zero means continuous); scan and retry with bounded backoff. |
| `status` | Report accepted frontier, peer acknowledgments, review/installation state and explicit namespace/content conflicts. |
| `revoke` | `peer`; remove local authorization for subsequent sessions. |

For a real LAN connection, Bob's listener request can be:

```json
{"action":"serve","directory":"/Users/bob/.lambda-sync/family",
 "port":41023,"bind":"192.168.1.20","max_sessions":0}
```

Alice uses `sync` or `watch` with
`"url":"ws://192.168.1.20:41023/sync"` and Bob's fingerprint in `peer`.
An explicit `"bind":"0.0.0.0"` listens on all IPv4 interfaces; omission/null
keeps the transport's loopback default. Numeric IPv4 addresses are required for
binding. Local routing/firewall permissions must allow the chosen port.

Runnable independent-process acceptance gate:

```sh
python3 test/sync_peer/test_peer.py
LAMBDA_EXEC_BACKEND=jit python3 test/sync_peer/test_peer.py
```

The gate creates all artifacts under `temp/sync-peer`. It uses separate processes,
folders and wrapping keys, and tests a non-loopback host interface when available.
It is not evidence of testing on two separate physical machines or other OSes.

## Authentication and encryption

`peer_identity` builds signed public descriptors from separate Ed25519 and X25519
seeds. A fingerprint is lowercase SHA-256 of the Ed25519 public key. The descriptor
signature binds both public keys. Explicit invitation acceptance pins the inviter;
discovery or learning an address grants no access. Enrollment offers use ephemeral
X25519, HKDF-SHA256 and AES-256-GCM to carry the space key to the intended recipient,
and Ed25519 to authenticate the whole offer. Private credentials are an authenticated
encrypted row in `device.sqlite`. Enroll each device separately; copying this
database clones its cryptographic identity. Membership mutations load the current row and
write its replacement in the same SQLite scope; obsolete file snapshots cannot
blindly replace current membership. Cross-process writer contention is not qualified
([LI-015](../../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-015)); run
administrative mutations sequentially. Contention and uncertain commits surface
as errors; reopen and inspect status rather than blindly retrying. Caller-supplied wrapping keys are never protocol fields.

The session profile is `lambda-peer-lan-1`:

1. Client and server exchange bounded hellos containing version, space, device
   fingerprint, role, fresh nonce and ephemeral X25519 public key.
2. Each signs a domain-separated transcript containing both ordered hellos and
   its role. Each requires an explicitly authorized peer; clients additionally pin
   the expected endpoint. No repository data is sent before mutual verification.
3. X25519 produces the shared secret. HKDF extract uses SHA-256 of the transcript
   as salt; one-block expand uses distinct client/server labels.
4. AES-256-GCM encrypts every request/response. Additional authenticated data binds
   the profile, transcript digest, sender role and exact sequence number. Replay,
   reflection, reordering and cross-session messages fail authentication.

Native OpenSSL primitives follow [Ed25519](https://docs.openssl.org/3.5/man7/EVP_SIGNATURE-ED25519/)
and [X25519](https://docs.openssl.org/3.5/man7/EVP_PKEY-X25519/); goldens pin
[RFC 8032 §7.1](https://www.rfc-editor.org/rfc/rfc8032.html#section-7.1) and
[RFC 7748 §6.1](https://www.rfc-editor.org/rfc/rfc7748.html#section-6.1) vectors.
Key derivation follows [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869.html).
Private provider contexts exclude ambient OpenSSL configuration; low-order
X25519 peers are rejected. The application handshake is an experimental protocol,
not a standardized or independently audited secure-channel implementation.

The WebSocket link may use WS because application records are encrypted and
authenticated. Initial device/space identifiers, traffic size and timing remain
visible. WSS client certificate verification remains available. The native
listener currently has no TLS termination. This LAN adapter has no central relay,
persistent server-side socket or cloud function dependency.

## Replication and recovery

`peer_exchange.offer` captures one immutable compact workspace checkpoint,
its existing protocol frames, a portable object inventory and a checkpoint proof.
The connection pins that offer; repository edits after it require a subsequent
exchange. Read-only private state and recovery journals never enter the inventory.

Offer version 3 also pins a [live-document catalog](DOCUMENTS.md). Unsaved Text
and Mark edits use atomic protocol deltas; compacted prefixes use checkpoint
frames. New document/saved-history roots use the existing immutable object
fragments. Historical document objects are excluded from the bulk peer inventory
so an ordinary edit need not transfer a full document snapshot. The shared
full-space writer grant authorizes signed relay attestations for these deltas.

The client pulls inventory pages and missing object fragments, commits verified
objects, then receives the signed checkpoint frames. The final frame cannot apply
references whose dependencies are absent. It then offers its merged state back to
the server, which requests only missing objects. Both retain pending local work
through the existing CRDT merge; neither replaces its live state with remote state.

Checkpoint signatures use the existing `auth.checkpoint_bytes` domain and
`auth.admission`. Paired devices receive explicit checkpoint authority to attest
combined state. This profile sends checkpoints rather than claiming a relay authored
another device's operations. Original transaction-signature relay and incremental
metadata optimization remain separate work. File payloads still transfer by
missing immutable objects, preserving chunk deduplication across versions.

Imported objects persist independently. Partial object text is connection-local
and retransfers after reconnect; fully imported objects are reused. Checkpoint
frame progress commits in the encrypted local head and resumes through the
existing protocol. The next connection renegotiates inventory and checkpoint
identity, so a changed sender does not splice two different checkpoints together.
Restarted handles allocate fresh writer prefixes without rewriting earlier IDs.

An ACK means the receiver completed the existing repository transaction, including
referenced content. Peer frontiers persist through `local.acknowledge`; neither
receipt nor peer ACK means cloud persistence. The local repository's
[power-loss qualification limit](LOCAL_REPOSITORY.md#objects-and-encryption) remains.

Files with competing content heads and captured external inputs require explicit
resolution. `status.conflicts` exposes both revision descriptors; automatic
installation pauses and leaves working bytes intact. Existing `local.history`,
`read_content`, `publish` and `resolve_review` provide review/resolution APIs.
Network success and successful filesystem installation are separate outcomes.

## Bounds and deferred work

Hellos are bounded to 16 KiB; session records to 256 KiB. Inventory pages contain
at most 64 IDs, object text fragments at most 32 KiB, and encoded objects at most
8 MiB. Each connection pins at most 100,000 portable objects and 8,192 checkpoint
frames, with 100,000 server requests and 1,000,000 encrypted records per direction.
Handshake waits use 15 seconds; record waits use 30 seconds. Existing repository,
typed-codec, scan and metadata-frame limits also apply.

The initial server services one connection at a time. The native agent reloads current membership before sending records and before
applying received records, including within established sessions. A revoked peer
therefore fails its next request. Revocation cannot erase previously received data.
Low-level callers of `peer.handshake`/`accept`/`synchronize` supply an equivalent
`authorize(peer_id)` callback when their grant set may change. Session keys and private credentials are not serializable
public application state. Secure memory erasure in the script heap is not claimed.

The qualified host is macOS native CLI. Native WebRTC/ICE and automatic Bonjour
discovery are implemented in the connectivity profile; physical cross-NAT,
browser and other-OS qualification remains separate. Partial/read-only space
grants, file-only shares, key rotation, OS-vault integration and editor UI remain
subsequent milestones. S3 publication/polling is implemented in the cloud adapter;
hosted-provider qualification remains environment-specific. This adapter preserves the
transport-independent CRDT/repository formats for those integrations.

Implementation and qualification: [completed native peer milestone](<../../../vibe/impl/Lambda_Impl_Sync_Peer (done).md>).

## Retention interoperability

Offers include authenticated retirement and deferred-cache metadata, alongside
the document catalog. Import retirement before admitting checkpoints; partial
frames keep the existing branch until a complete accepted replica is available.
Expired payloads do not re-enter shared inventory when an old device reconnects.
Retained ancestry/CRDT anchors still merge its pending changes. A peer may relay
deferred historical chunks; live/base/pending data must remain available locally.
Hydrate cold chunks from their verified cloud provider before restoring them.
See [maintenance](MAINTENANCE.md). Older offers remain readable, but all writers
and the cloud handler must upgrade before enabling retention (D11.1.2, D11.2.3).

The large-file profile uses manifest v2, paged descriptors and authenticated raw
chunk objects. Upgrade all participating clients before using it. The agent scans
once before publishing listener readiness and reuses exact unchanged-file
observations on subsequent scans. Connection deadlines remain unchanged; bulk
installation streams chunks through the native atomic writer (D11.1.2).
