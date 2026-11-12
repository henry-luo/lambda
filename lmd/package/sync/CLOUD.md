# Durable cloud synchronization

This native profile adds encrypted object-storage publication, periodic HTTP
fallback, cloud-only restoration and configurable saved history to the [peer agent](PEER.md).
It uses the existing local repository, content IDs and merge-safe checkpoints.
Direct peer receipt, cloud durability and filesystem installation remain distinct.
Policy is Lambda Script (S12.1.1v3, D9.3.4); existing native HTTP/crypto/SQLite
mechanisms cross D11.1.2. No new C++ or language semantics are required.

## Deployment

The deployment consists of a private S3-compatible bucket and one HTTP function.
`cloud_function.handle` is the application handler. Each invocation authenticates
its request and reads durable state from the bucket. It does not wait for an
update, sleep, use a server-side polling loop, retain an application session or
hold a WebSocket. Clients poll periodically. The bucket retains content and history according to explicit policy; function memory and local function files are disposable.

[HTTP adapter and deployment](../../../deploy/sync_cloud/README.md) provides a
one-request process interface, a function-URL event wrapper and a local/container
HTTP entry. Provider infrastructure forwards HTTP requests to this handler;
a separate application API gateway is unnecessary where a direct HTTPS function
endpoint is available. HTTP processes used by local tests are development adapters.
Hosted deployment, billing and platform execution limits require that platform's
qualification; none is implied by local acceptance.

The service receives `LAMBDA_CLOUD_STORAGE`, a private JSON environment value:

```json
{"endpoint":"https://objects.example.net","bucket":"lambda-sync",
 "region":"us-east-1","access_key":"SERVICE_ACCESS_KEY",
 "secret_key":"SERVICE_SECRET_KEY"}
```

An optional `session_token` supports temporary S3 signing credentials. Endpoints
use path-style bucket requests and have no trailing slash or base path. The
service principal needs GetObject/PutObject/HeadObject/DeleteObject for the selected bucket's
`spaces/` prefix. Do not grant clients the service credentials. Clients never receive DELETE capabilities. Owner-authorized maintenance
allows the function to delete only retired physical storage generations. Bucket lifecycle rules must not expire live
objects, receipts, control records or publications. Recovery from storage administrator changes or infrastructure loss requires
remaining authenticated bytes and an explicit catalog backup.

HTTPS verification stays enabled and signed requests never follow redirects.
Loopback HTTP is permitted only for explicit local tests. Other S3 dialects and
hosted backends require their own condition/checksum/signing checks. Conditional
ETags are opaque version tokens, never content hashes. The adapter implements
bounded object GET/HEAD, conditional writes and presigned capabilities; multipart,
range resume and enumeration-based maintenance are outside this bounded profile.

## Setup and client requests

Create and enroll native devices with the existing [peer CLI](PEER.md#run-the-agent).
Each device has independent encrypted credentials and wrapping key. Its private
agent directory stays outside its working tree. The space owner is pinned by
fingerprint; addresses and discovery never grant membership (D11.2.3).

The owner/operator bootstraps cloud control once using `cloud_cli.ls`. Supply the
service storage environment only for this operator command. Public descriptor
files in `members` contain the other explicitly authorized devices:

```json
{"action":"bootstrap","directory":"/Users/alice/.lambda-sync/family",
 "members":["./bob.device","./recovery-device.device"]}
```

Use the same invocation convention as the peer CLI:

```sh
LAMBDA_SYNC_REQUEST=./temp/cloud-request.json \
LAMBDA_SYNC_KEY="$ALICE_SYNC_WRAPPING_KEY" \
./lambda.exe --no-log run lmd/package/sync/cloud_cli.ls
```

The request file contains public configuration. Wrapping keys and service
credentials are separate environment values and never protocol fields. Normal
clients require only their own wrapping key, the function URL, pinned owner and
previously enrolled device credentials:

```json
{"action":"sync","directory":"/Users/bob/.lambda-sync/family",
 "cloud":{"url":"https://function.example.net/sync","authority":"OWNER_FINGERPRINT"},
 "direct":{"url":"ws://192.168.1.20:41023/sync","peer":"ALICE_FINGERPRINT"}}
```

Omit `direct` for cloud-only sync. One round attempts the supplied peer first,
then checks/publishes cloud state independently for long-term storage. Direct
failure falls back to HTTP; transport changes keep the existing CRDT identifiers.
A scan failure never publishes a deletion inferred from an unavailable root.

| Action | Additional fields and behavior |
|---|---|
| `sync` | `cloud`, optional `direct`; one exchange and installation attempt |
| `watch` | Same; `interval_ms` defaults to 5000, `cloud_every` to 6, `rounds` to 0 (continuous). Healthy peers can exchange between cloud checks; direct failure checks HTTP immediately. |
| `run` | `cloud`, `port`; optional `bind` (loopback default), `direct`, `interval_ms`, `rounds` and `ready`. One native owner services incoming peer sessions and periodic cloud work. |
| `status` | Existing local status, peer frontier, review/conflict/installation state and known cloud coverage |
| `membership` | Owner only: `cloud`, `recipient` descriptor path, `active` boolean. Add/reactivate or revoke through a conditional signed control update. |
| `history` | `cloud`, `actor` device fingerprint, optional `start` publication ID and `limit` (default 32, maximum 128); returns immutable publication metadata and a continuation |

`watch` is client polling: calls do not overlap, failures back off to at most
60 seconds, and each new round reloads state. `run` completes each peer session before scheduled cloud work, with at most a
one-second idle accept wait. A busy session can delay its timer. Its `rounds`
limits listener iterations, while zero runs continuously; `ready` writes the
selected listener port. The input/lib context-unlink fix resolves the earlier
shutdown stall ([LI-016](../../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-016));
the combined command is included in the real-S3 gate on every execution tier.
Use one active agent writer per private directory. Concurrent administration remains unqualified under
[LI-015](../../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-015);
do not run administrative mutations concurrently or retry uncertain commits.

`sync.cloud_saved` covers the current workspace frontier at the configured
provider. Local status reports previously confirmed cloud coverage; successful
peer delivery alone never advances it. Pending filesystem review and competing
content heads pause automatic installation and preserve working bytes. A later
edit remains pending even when an older captured frontier was cloud-saved.

## Protocol and storage

The service has S3 credentials and signed public membership records. It never
receives repository encryption keys, wrapping keys or decrypted checkpoints.

1. A request is an Ed25519-signed typed envelope binding protocol version, space,
   device, operation/body, fresh nonce and millisecond issue time. The function
   requires current authorization and a clock difference of at most five minutes.
   Nonces do not imply a server replay cache. A repeated request rechecks current
   membership; immutable creates, signed sequences and CAS make it idempotent or stale.
2. The signed control record pins an owner and up to 64 full-space writer/checkpoint
   authorities. Removed devices remain historical public identities with
   `active:false`; they cannot make new authorized requests. Clients persist
   observed control generations and reject rollback or same-generation forks.
3. Clients upload encrypted immutable repository objects directly through scoped,
   60-second S3 PUT capabilities. Signed create-only, checksum and length headers
   constrain each grant. Existing objects are authenticated locally before reuse.
4. Checkpoint bundles encrypt saved CRDT state, its checkpoint proof, object
   inventory and named snapshot IDs. A signed publication binds the ciphertext
   digest, clock and dependency commitment. The function cannot inspect plaintext;
   the authorized checkpoint writer explicitly attests its encrypted graph.
5. A bounded `stage` validates at most 64 immutable dependency keys and stores a
   server-only immutable receipt. Receipts may be reused within their physical storage epoch.
   Clients cannot upload a receipt through an object capability.
6. `commit` checks up to 128 receipts, verifies their combined dependency commitment,
   stores the immutable signed publication, then conditionally replaces the per-space catalog containing that
   device's head. Publication and maintenance share this same CAS fence. The signed sequence and previous-publication digest preserve
   per-device history. A stale head returns a conflict; it is never overwritten.
7. Only a complete discoverable publication advances durable local cloud coverage.
   The local journal records provider-specific positions, covered clocks and
   object inventory through the existing repository transaction.

Object paths are beneath `spaces/<SHA256(space)>/`:

| Key | Contents |
|---|---|
| `control` | Signed owner/membership control, conditionally replaced |
| `catalog` | Conditional per-space heads, epoch, reservations and maintenance cursor |
| `catalogs/<digest>`, `catalog-required` | Immutable recovery backup and fail-closed protocol marker |
| `objects/[<epoch>/]<repository-id>` | Authenticated encrypted chunks, manifests, document states and snapshots/checkpoints |
| `bundles/[<epoch>/]<ciphertext-hash>` | Encrypted merge-safe workspace, document roots and inventory |
| `receipts/<digest>` | Immutable server-validated dependency list |
| `publications/<digest>` | Immutable signed publication, including previous-publication link |
| `heads/<device-id>` | Legacy upgrade input; the catalog is authoritative in this profile |

Files retain their stable IDs across rename/move. Existing content-defined chunks
and immutable identities deduplicate saved versions. This profile publishes
signed checkpoint streams; it does not claim to implement original-authored
transaction relay or replace the existing checkpoint-authority grant.

Bundle version 3 includes [durable live-document roots](DOCUMENTS.md). Their
accepted CRDT edits replicate independently of saved file bytes. Document state
and its attachment/saved-version dependencies must validate before cloud coverage
is recorded. Root identity is checked as well as edit clocks; a new saved version
can require publication even when its live frontier is unchanged. Version-1/2
bundles remain readable by the new adapter. Version 3 also carries
retirement and deferred-history availability metadata.
`status` accepts an optional `cloud` configuration for provider-specific local
coverage. Its top-level `cloud_saved`, synchronization results and the combined
owner all include pending document roots as well as workspace progress.

Pull reads all roster heads each time. Local positions advance only after the
bundle and required objects are authenticated, checkpoint frames apply and the
local transaction commits. Each subsequent pull scans the roster again, so a
concurrent publication is discovered on a later poll without a global LIST cursor
skipping it. Previously read heads and controls cannot move backwards locally.

## Recovery and durability

Interrupted object uploads are retried as bounded immutable objects. Complete
objects and validated receipts are reused. A failed upload or missing dependency
cannot advance a head. An interrupted client reopens its local repository and
pulls durable heads before publishing again.

A lost commit response is an inspection case: the next pull finds the existing
signed publication. The client records that coverage without allocating another
logical edit or publishing a redundant version. Repeating the exact commit is
also idempotent. Function replacement needs no memory or local database recovery.

A fresh **already authorized** device with its own credentials/space secret and
pinned owner can restore the current workspace, file history and named snapshots
while original peers are offline. The snapshot IDs support `local.restore` into a
separate empty target. An offline invitation can prepare that device in advance.
This is not automatic account/key recovery: a public fingerprint alone cannot
decrypt a backup, and copying a credential database clones an identity.

Revocation rejects new control requests. A capability already issued remains
usable until its expiry, and an admitted publication can race a concurrent
revocation. The pre-publication control reread narrows the race but is not an
atomic revocation fence. Previously downloaded data cannot be recalled. Cloud membership and existing local P2P trust are separate grants in this profile:
revoke both where both were granted. Cloud membership does not silently expand
local peer trust. There is no immediate global cutoff, key rotation, partial
sharing or independent security-audit claim in this profile (D11.2.1–D11.2.4).

Cloud durability relies on the qualified storage backend's acknowledged writes
and correct retention fencing. It does not upgrade the local repository's separate
[power-loss qualification](LOCAL_REPOSITORY.md#objects-and-encryption).

## Audited recovery and reusable storage

New spaces default to `storage_protocol:"catalog-2"` at bootstrap.
An explicit `"catalog-1"` remains available for legacy qualification. Existing v1
spaces remain readable; an owner audit upgrades them. Physical object paths are
recorded separately from logical content IDs in private encrypted locator pages.
A maintenance cut retains registered immutable copies at their existing paths,
copying only unpublished data. Immutable retirement jobs bind old and keep roots;
old workers cannot delete an object that a later publication re-admits, because
retired physical paths cannot be re-admitted. Generation allocations are monotonic,
including aborted jobs and claims interrupted before catalog activation.

After a missing catalog, `backup_discover` returns bounded candidate metadata
(`cursor`, `limit` 1–16). The owner chooses an authenticated backup ID and calls
`catalog_recover` with `snapshot`; replacing an existing unaudited candidate also
requires its exact `expected` ETag. Discovery does not determine the latest backup
or recover lost keys. A rollback seen by a surviving client remains an error.

The owner runs `audit_begin`, then bounded `audit_scan` calls (budget 1–64) until
discovery completes, then `audit_step` (budget 1–8) until verification completes.
Each step persists its cursor; reopening the CLI resumes it. If the service began
an audit before the local record was saved, another `audit_begin` adopts that
frozen job and restarts client verification. `audit_status` reports local progress,
bad-object count and up to 32 sample paths. `audit_abort` leaves cleanup disabled
and permits a fresh inventory after explicit repair.

The client uses observed ETags in signed conditional GETs, validates object MACs,
content IDs and bundle proofs, then re-reads every committed dependency and checks
the local retained graph. Failed/missing/changed data cannot yield an attestation.
`audit_prepare` attaches the signed report to a replacement cut; continue with
`maintenance_step`, then bounded `maintenance_prune`. This audit cut copies the
retained graph once into a fresh generation and enables subsequent reuse/pruning.
Unknown unpublished bytes remain quarantined. After the cut completes,
`audit_abort` clears its finished local audit record. Listing is bounded discovery, not a
transactional bucket snapshot or proof that a selected backup is globally latest.
The function sees encrypted data; only the owner client can authorize promotion
(D11.2.1–D11.2.4).

## Bounds and verification

Function requests are at most 128 KiB; responses at most 1 MiB. Objects and
checkpoint envelopes are bounded to 8 MiB; each publication references at most
8,191 repository objects plus its bundle in legacy catalog v1. Catalog v2 uses
immutable 64-entry/32-child metadata pages and signed receipt roots, with up to
65,536 repository object paths plus the bundle per publication. Individual
workspace/checkpoint envelopes still have the 8 MiB bound. File contents use
streamed chunk manifests; existing codec/repository metadata limits apply. Upload restarts operate at object granularity. Saved history has paged
ancestry; aggregate publication inventories and individual
endpoints retain these bounds and are never silently truncated.

Focused fixtures are `sync_cloud_crypto`, `sync_cloud_protocol` and
`sync_cloud_repository`. The [process gate](../../../test/sync_cloud/test_cloud.py)
uses real S3, separate repositories/processes, a new function process per request,
host restarts and a deliberately dropped committed response. Test-store setup
and qualification receipts are in the [implementation plan](<../../../vibe/impl/Lambda_Impl_Sync_Cloud (done).md>).
Hosted-cloud, Linux/container execution, physical two-device and independent
security qualification remain environment-specific work.

The adapter follows [S3 SigV4 header authentication](https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html),
[presigned query authentication](https://docs.aws.amazon.com/AmazonS3/latest/API/sigv4-query-string-auth.html),
[conditional writes](https://docs.aws.amazon.com/AmazonS3/latest/userguide/conditional-writes.html)
and [PutObject checksums](https://docs.aws.amazon.com/AmazonS3/latest/API/API_PutObject.html).
HMAC vectors follow [RFC 4231](https://www.rfc-editor.org/rfc/rfc4231.html);
the SigV4 golden also matches an independent Python HMAC/hash calculation.

## Retention, cache and catalog recovery

[Maintenance](MAINTENANCE.md) defines configuration, pins, grace, bounded
`maintenance_*` actions, explicit scheduled `maintenance_tick` calls and
`cache_*` eviction/hydration actions. Permanent retention remains the default.
Every cut copies retained roots into a fresh physical epoch before allowing old
key deletion; clients still use one stateless HTTP function and periodic polling.

Pull captures all heads from one catalog version and returns its immutable
`catalog_backup` ID, persisted by clients per provider. If the catalog disappears,
the service rejects normal operations. Owner-only `catalog_recover` with
`snapshot` restores a selected validated backup; destructive maintenance stays
disabled until an inventory audit. Latest-backup discovery/audit tooling is not
implemented. Stop/drain old function invocations and upgrade all writers before
enabling retention; legacy untracked stores also require that audit. Active
upload reservations require their writer to finish or explicitly cancel.
