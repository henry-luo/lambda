# Native local sync repository

The experimental local agent is primarily Lambda Script. `lambda.sync.local`
combines the document/workspace protocol with encrypted immutable storage,
complete filesystem scans, external-change reconciliation, installation journals,
saved checkpoints, snapshots, verification and restore. The deterministic policy
stays in scripts under S12.1.1v3; `pn` owns effects under S12.4.4v4. Native APIs
provide cryptography and anchored filesystem access through D11.1.2.

This is the local milestone of [the sync design](../../../vibe/Lambda_Design_Data_Sync.md#121-file-sync-and-periodic-fallback).
It does not deploy a network service. A trusted host supplies keys and authenticated
message admission; it can call scans periodically. The [native peer adapter](PEER.md)
now supplies explicit pairing, authenticated LAN transport and periodic retry.
The [cloud adapter](CLOUD.md) adds object publication, periodic HTTP fallback
and fresh-device recovery. The [document host](DOCUMENTS.md) persists unsaved
sessions and links exact-frontier saves to workspace revisions. Native filesystem
watchers and UI bindings follow separately.

## Start a repository

```lambda
import local: lambda.sync.local
import crypto: lambda.io.crypto

pn main() {
    io.mkdir("temp/local-demo/tree")^
    output("hello", "temp/local-demo/tree/note.txt")^
    let key = crypto.key()^
    let agent = local.create("temp/local-demo/repo.sqlite", key,
        "demo-space", "temp/local-demo/tree", "My device")^
    local.scan(agent)^
    local.snapshot(agent, "First capture")^
    local.verify(agent)^
}
```

Creation requires a new database and an empty object directory. Store the key
through the trusted host's key-management policy; the example keeps a temporary
test key only in memory. `open_repository(path, key, space)` reopens an existing
repository. Each handle gets a fresh random writer prefix, so copied/rolled-back
stores do not resume the same actor stream. The device label and local serial
are persisted. A repository and its object directory must be outside the working
tree; restore targets must also be separate. Detected root replacement fails explicitly.

Runnable two-replica and recovery examples with assertions:

```sh
./lambda.exe run test/lambda/proc/sync_local_filesystem.ls
./lambda.exe run test/lambda/proc/sync_local_recovery.ls
./lambda.exe run test/lambda/proc/sync_local_document.ls
```

The exchange helper in those tests owns both local repositories and explicitly
admits their actors/checkpoints. It is test code, not network authentication.

## Local API

Mutations and `status` return `{generation, id, state}`. Handles contain secret
key material and must not be serialized, logged or shared as application state.

| API | Contract |
|---|---|
| `create(path, key, space, root, device, chunker?)` | Create the local repository and empty workspace; return a handle. |
| `open_repository(path, key, space)` | Authenticate and validate existing state; return a fresh writer handle. |
| `status(handle)` | Read the committed local state without advancing it. |
| `acknowledge(handle, peer, frontier)` | Validate and persist a peer's accepted causal frontier; this does not mark it cloud-saved. |
| `record_cloud(handle, provider, actor, sequence, publication, captured, objects?, documents?)` | Record host-verified workspace/document coverage and per-provider progress after validating dependencies. |
| `register_documents(handle, roots)` | Validate imported shared document objects and merge their sessions without overwriting the local disk base. |
| `record_cloud_control(handle, provider, generation, digest)` | Retain observed signed-control position; reject rollback/forks. |
| `register_snapshots(handle, ids)` | Verify imported named snapshot objects and retain their roots locally. |
| `scan(handle)` | Capture the whole tree, reconcile from the accepted disk baseline, and commit content before the new head. |
| `transact(handle, commands)` | Apply existing workspace commands; referenced content must already be available. |
| `publish(handle, entry, bytes, document?)` | Store file bytes and publish a saved revision, preserving observed competing parents. |
| `register_checkpoint(handle, saved)` | Store a validated CRDT checkpoint and return its workspace document link. |
| `prepare_install(handle)` | Capture external changes, then persist an installation intent pinned to a workspace frontier. |
| `install_step(handle)` | Execute/reconcile one journal step; commit progress or a blocked recovery capture. |
| `materialize(handle, max_steps=1000)` | Prepare/resume installation until complete, blocked, or the step budget ends. |
| `resolve_review(handle, commands)` | Explicitly author a branch matching a retained external capture, then merge it with live work. |
| `history(handle, entry)` | List immutable file revisions, including losing alternatives and renamed files. |
| `read_content(handle, reference)` | Authenticate chunks and reconstruct exact file bytes. |
| `snapshot(handle, label)` | Pin a saved workspace snapshot in local state. |
| `restore(handle, snapshot, destination)` | Restore a snapshot into a separate empty directory; refuse competing unresolved file heads. |
| `verify(handle)` | Authenticate physical objects, check locators, reconstruct all manifests, and validate current workspace/snapshot dependencies. |
| `recover(handle)` | List authenticated unindexed objects and validated local-state candidates; never advance a live head automatically. |
| `rebuild_index(handle)` | Rebuild the SQLite object locator table from authenticated immutable files and verify it transactionally. |

An idle scan or installation does not create a file revision. Installation is a
separate effect from repository publication. A save captures its chosen document
frontier; later CRDT edits remain live and are not retroactively included. The
[document fixture](../../../test/lambda/proc/sync_local_document.ls) checks both
levels. Protocol checkpoint digests map to scoped repository object IDs through
`repository.object_id`; they are not treated as physical filenames directly.

## Objects and encryption

`<database>.objects/` holds canonical, create-only object files named by their
32-byte scoped ID in lowercase hex plus `.object`. SQLite stores a derived
`objects` locator table and an encrypted generation-checked `heads` record.
Paths, manifests, workspace state and private recovery journals are encrypted;
SQL exposes object IDs, generic kinds, sizes and head generations. Editable
working files remain ordinary files.

The format is independent of SQLite and cloud locations:

```text
LRP1 (4 bytes) | kind (1 byte) | logical ID (32 bytes) | encrypted envelope
kind: 0 chunk, 1 manifest, 2 private state, 3 snapshot, 4 checkpoint, 5 document
encrypted envelope: LGC1 | random nonce (12) | GCM tag (16) | ciphertext
```

AES-256-GCM uses a fresh random nonce for each new physical encryption.
HMAC-SHA256 derives distinct space-scoped encryption and identity keys from the
caller-supplied 32-byte secret. Logical IDs are keyed hashes of the typed canonical
plaintext and bind the space and object kind. GCM additional authenticated data
binds the repository version, space, kind and logical ID. Heads additionally bind
their generation. Native code uses the existing mbedTLS provider;
[authenticated decryption](https://mbed-tls.readthedocs.io/projects/api/en/v3.6.3/api/file/gcm_8h/)
verifies the tag before exposing plaintext. The tests include NIST AES-GCM and
[RFC 4231 HMAC vectors](https://www.rfc-editor.org/rfc/rfc4231.html#section-4.2),
wrong key/context, truncation and tampering.

Object files are synced and read back before the referencing SQL transaction can
commit. A failed transaction can leave harmless unindexed immutable files; recovery
enumerates them. An uncertain commit must be inspected after reopening, not blindly
retried. Existing SQLite barriers qualify transaction/process-restart behavior;
**power-loss durability remains unqualified**, as documented by the
[SQLite API](../../../doc/Lambda_Sys_Func.md#lambdaiosqlite). No protocol durability
ACK is invented by these APIs. Lost head state requires selecting a recovery
candidate; rebuilding locators alone does not infer the latest intended head.

History and immutable files are retained permanently in this milestone. Verification
authenticates old private state objects without replaying every old editor or
installation state. Private states are excluded from object exchange; portable
file/revision history remains represented by workspace checkpoints and manifests.

## Chunking and exchange

`lambda-gear32-1` uses the Gear rolling fingerprint described by the
[FastCDC work](https://www.usenix.org/conference/atc16/technical-sessions/presentation/xia),
without claiming FastCDC wire compatibility or its normalization optimizations.
Its table is the first four SHA-256 bytes, big-endian, of
`"lambda-gear32-1:" ++ string(byte)` for byte values 0–255. Starting at zero,
`fingerprint = (2*fingerprint + table[byte]) % 2^32`; cut after the minimum when
`fingerprint & (target-1) == 0`, or at the maximum, then reset the fingerprint.
The initial profile is 1 KiB minimum, 4 KiB target and 16 KiB maximum. Parameters
are stored with each manifest and must use a power-of-two target. This is a
reference profile; release tuning and packing are later work.

Manifests retain ordered chunk IDs/lengths, total size, profile and whole-file
SHA-256. Empty files have a manifest with no chunks and null byte content
(S2.2.2v2, S2.2.4). Insertion and byte-exact reconstruction are tested. Reuse is
scoped to the space; encryption does not use public plaintext hashes as keys.

`inventory` advertises portable object IDs. Transfer missing IDs using
`export_objects`/`import_objects` in batches of at most 128. Import authenticates
objects and keeps existing verified ciphertext; it does not advance workspace
heads. Exchange `messages(handle, workspace.have(peer.endpoint))`, then pass each
packet to `receive(handle, packet, admission)`. Checkpoint frames and atomic
transactions reuse the existing protocol. The host must provide authenticated
actor/checkpoint admission and transfer dependencies before applying references.

## Filesystem behavior and current limits

Scans include hidden entries and fail completely on missing/replaced roots,
unsupported entries, invalid names, capture limits or detected races. They do not
interpret a failed scan as deletion. Native identity evidence associates moves;
equal content alone never establishes identity. Save-by-replacement at an existing
path retains file identity. Type changes retain a review capture until explicit
commands map that capture into the workspace. Case-folding path collisions reject
materialization rather than choosing a platform-dependent winner.

Installation creates directories, writes files and removes obsolete entries in
recoverable steps. It captures accepted preimages before replacing files, uses
native revision tokens, verifies readback and checks empty directories before
removal. A detected external write blocks the journal and retains recoverable
content; retry after review does not discard the capture. Current callers restore
the expected preimage or desired content before resuming. Arbitrary edits during
a blocked partial installation are not automatically rebased yet. Native revision
checks detect substitution; they do not exclude every concurrent external writer
in the final filesystem publication window. Multi-file installation is not
simultaneously visible to external applications.

The first host qualification is macOS native CLI. File anchors fail closed on
unsupported platforms or with a bound security client lacking the broker operation.
Limits include 10,000 scan entries, 128 directory levels, 8 MiB per file/physical
object, 64 MiB captured bytes per scan, 100,000 physical objects per repository
enumeration, and the existing 8 MiB typed-codec/16 MiB SQLite statement bounds.
File metadata beyond names, kinds and payload bytes
(permissions, xattrs, hard links, sparse extents and symlinks) is not synchronized.
Unresolved binary/file content alternatives remain in revision history and block
installation until explicitly resolved; automatic conflict-copy naming is later work.

Native calls use their qualified spelling. The existing
[LR07-69](../../../vibe/Lambda_Issue_Ledger.md#lr07-69) gap prevents dynamic calls
through builtin reference values.

Keys/enrollment, remote trust, file-link isolation, key recovery/rotation, browser
execution, physical pruning and production performance are separate milestones.

Implementation and qualification: [completed local milestone](<../../../vibe/impl/Lambda_Impl_Sync_Local_Repository (done).md>).
