# Device Sync over Passive Object Storage — Idea

**Status:** Idea, 2026-10-22. Not on the roadmap and not implemented. It
needs the user rulings listed in §9 before it can become a design.
**Context:** The user asked whether several devices could sync through S3
alone. The storage would be passive: it stores and serves objects and runs no
code. The devices would do all the pulling, merging and cleanup themselves.
The question came up while the [mail demo](../Lambda_Demo_Mail.md) was being
started. Its store is raw message files plus a Mark index per mailbox
(ML7v3). POP3 accounts keep their flags and folders only on the local disk
(SL2), so a second device has no way to see them.
**In one line:** yes. S3 has had conditional writes since 2024, and one atomic
compare-and-swap on one key is enough to build any coordination a server
would do. Merging is the easy part. The hard parts are cleaning up after
devices that stay offline, crashes in the middle of a write, polling instead
of push, and storage that cannot be trusted.

---

## 1. The short answer

A sync server does three jobs. It **orders** concurrent writes, it **merges**
them, and it **tells** devices when something changed.
- **Ordering** needs one atomic operation on the storage. S3 now has two
  (§2).
- **Merging** can run on every device, if the merge is deterministic. Every
  device that has seen the same set of changes then computes the same state.
- **Telling** has no passive replacement. Devices poll a small object
  instead (§6.4).

Theory agrees. With compare-and-swap, one key behaves as a linearizable
register, and that is enough for consensus. Even plain read/write disks are
enough: Disk Paxos (Gafni and Lamport, 2000) reaches consensus with passive
disks only. Nobody needs to build that on S3 any more.

## 2. What the storage must provide

| Primitive | S3 since | Use |
|---|---|---|
| Strong read-after-write consistency, including LIST | December 2020 | A write by one device is seen by the next read or list of any device. There is no window in which an object is missing or stale. |
| `PutObject` with `If-None-Match: *` (create only if absent) | August 2024 | Claim a log slot or a sequence number. The first writer wins. Every later writer gets `412 Precondition Failed`. |
| `PutObject` with `If-Match: <etag>` (compare-and-swap) | November 2024 | Move a mutable pointer, such as a head or the current snapshot, atomically. |
| Immutable objects, optional versioning | always | Content-addressed blobs never conflict. Versioning is a safety net against a buggy or hostile device. |

**Before 2024 this needed a second service.** Delta Lake's multi-writer
tables on S3 used DynamoDB for exactly one thing: creating the next log file
only if no other writer had created it first.

**Other stores.** Google Cloud Storage has the same power through generation
preconditions (`ifGenerationMatch`, where 0 means "create only if absent").
Azure Blob Storage has ETag conditions and leases. S3-compatible stores (R2,
MinIO, B2 and others) differ in which conditional headers they honour. Each
target must be checked before a design relies on a header (Q1).

## 3. Design A — per-device logs and a deterministic merge

```
<root>/
  devices/<dev>/info.mark            name, public key, creation time
  devices/<dev>/log/000000042.ops    immutable batch of ops; only <dev> writes here
  devices/<dev>/cursor.mark          last seq written; version vector applied
  snapshots/<id>.snap                merged state covering one version vector
  SNAPSHOT                           points at the current snapshot; moved by compare-and-swap
  objects/<sha256>                   immutable blobs, such as message bodies
```

**One writer per key.** Every key under `devices/<dev>/` has exactly one
writer, so the log itself needs no conditional writes. Ordering comes from
the data, not from the storage.

**A sync round:**
1. Read every known device's `cursor.mark`, which is small. List
   `devices/` now and then to discover new devices.
2. Fetch the log batches this device has not applied yet.
3. Merge them into local state.
4. Write this device's new local changes as one batch at the next sequence
   number. `If-None-Match: *` guards against a cloned device ID (§6.6).
5. Write this device's `cursor.mark`.

One round costs one PUT for the batch and one for the cursor, however many
changes the batch holds.

**Merge rules.** Each op carries a hybrid logical clock (HLC) timestamp:
wall-clock time, a counter, and the device ID to break ties. The state is
built from CRDTs (conflict-free replicated data types), so the result does
not depend on the order in which batches arrive.

| Shape of data | CRDT |
|---|---|
| A single field, such as a flag or a folder | Last-writer-wins register ordered by HLC |
| A set, such as labels | Observed-remove set; a concurrent add wins over a remove |
| A count | PN-counter |
| Rich text | A sequence CRDT of the Automerge or Yjs kind |
| A deleted record | A tombstone with an HLC timestamp |

An illustrative op batch in Mark notation:

```lambda
<ops dev: "a1f3", seq: 42,
    <set id: "1781c0ffee2a", field: 'flags', add: ['seen'],
         hlc: "2026-10-22T09:14:00.000Z/0/a1f3">,
    <set id: "1781c0ffee2a", field: 'folder', value: "Receipts",
         hlc: "2026-10-22T09:14:02.000Z/0/a1f3">
>
```

**Least privilege comes for free.** IAM can let each device write only its
own prefix and read everything. A compromised device then cannot rewrite
another device's history.

**Best for** personal devices that are peers, work offline for long
stretches, and rarely touch the same record at the same moment.

## 4. Design B — one shared head moved by compare-and-swap

```
<root>/
  objects/<sha256>    immutable commits, trees and blobs
  HEAD                points at the latest commit
```

This is the git model, and the protocol `git push --force-with-lease`
follows:
1. Read `HEAD` and remember its ETag.
2. Merge locally: a three-way merge against the common ancestor, which the
   commit graph provides.
3. Upload the new objects. They are content-addressed, so uploading one
   twice does no harm.
4. Write `HEAD` with `If-Match: <the ETag from step 1>`.
5. On `412`, go back to step 1. Back off with jitter when retries repeat.

**Variant: a numbered log.** Each commit is written as `log/00000123` with
`If-None-Match: *`. The slot number is the lock and gives one total order. A
reader fetches `n + 1` until it gets `404`, so it never needs LIST. A
checkpoint every K entries bounds how much a new device must replay. This is
Delta Lake's commit protocol.

**Best for** data that must have one linear history, or data whose
conflicts cannot merge automatically. Such conflicts need a policy, such as
Joplin's conflict copies. The cost is contention: busy writers retry, and a
slow writer can starve.

## 5. Recommendation: A for the data, B's compare-and-swap in two places

- **Design A for the data.** It works offline, writers never contend, and it
  allows least-privilege credentials.
- **Compare-and-swap on the `SNAPSHOT` pointer.** Any device may write a
  snapshot. Only one becomes current.
- **Compare-and-swap for leases on singleton jobs.** Examples are garbage
  collection and, for mail, the POP3 fetcher (§8.3).

## 6. The hard parts

### 6.1 Compaction and garbage collection with offline devices

This is the real problem, not merging. A log entry can only be deleted once
no device will ever need it, and a laptop left in a drawer for six months
would block deletion forever.
- **Snapshots break the dependency.** Once a snapshot covers an entry, the
  entry can be deleted after a grace period. A device that is too far behind
  bootstraps from the current snapshot. It then re-applies its own
  unpublished local ops on top. That is sound only because CRDT merge
  combines whole states and does not care about order.
- **Tombstones are the subtle case.** A tombstone that is dropped too early
  lets a device that missed the delete bring the record back. Tombstones are
  kept until every active device's cursor covers them, or until a retention
  window has passed.
- **Stale devices stop counting.** A device whose cursor has not moved for
  the whole retention window no longer holds anything back. When it returns,
  it bootstraps. What happens to its unpublished edits of records deleted
  meanwhile is a policy choice (Q6).

### 6.2 A crash in the middle of a write

- **Data first, pointer last.** Write order is objects, then the log batch,
  then the cursor. For a snapshot, write the snapshot object, then move
  `SNAPSHOT`. A crash leaves unreferenced objects, never a pointer to a
  missing one.
- **Sweep with a grace period.** Mark everything reachable from the current
  snapshot and the retained logs. Delete only unreachable objects older than
  a grace period G, because a slow writer may be about to reference a fresh
  one. `git gc` and restic `prune` work the same way.

### 6.3 Leases are not safety

A lease is an object such as `locks/gc` holding a holder and an expiry. It is
taken with `If-None-Match: *` and taken over after expiry with `If-Match`.
Expiry times should come from S3's own `Date` and `Last-Modified` headers,
not from device clocks, which drift.

A lease still cannot make a job safe. A process can pause, wake after its
lease has expired, and carry on. So a lease only prevents duplicated work.
Correctness has to come from the commit itself: it must be a
compare-and-swap against the state the work started from. That ETag acts as
the fencing token.

### 6.4 No push

Devices poll. Polling one small object is cheap. Listing a prefix costs more.

| Poll | Every 10 s, per device per day | Approximate cost at S3 Standard list prices |
|---|---|---|
| GET one cursor or `HEAD` | 8 640 requests | about $0.004 |
| LIST a prefix | 8 640 requests | about $0.04 |

Poll fast while the app is in front or has just made a change, and slowly in
the background. S3 Event Notifications to SQS or EventBridge give near
real-time updates, but then the storage is no longer purely passive (Q5).

### 6.5 Storage that cannot be trusted

- **Encrypt on the device.** Authenticated encryption under a repository key
  covers every object. Key names are derived with an HMAC, so names reveal
  nothing.
- **Sign per device.** Each device signs its own batches with its own key.
  Ops are then attributable even if the storage, rather than IAM, is the
  attacker.
- **Detect rollback.** The storage can still serve an old state. Each device
  remembers the newest sequence number it has seen from every device and the
  newest snapshot generation, and refuses to go backwards. This is "fork
  consistency" (SUNDR, Li and others, 2004). It detects a rollback but cannot
  prevent one without a trusted third party.
- **Pair new devices out of band.** A new device needs the repository key,
  for example from a QR code or a passphrase run through a key-derivation
  function (Q4).

### 6.6 Device identity and credentials

- **A fresh device ID on every install.** An ID must never be restored from
  a backup. Two clones writing what each thinks is its own log is the one way
  design A gets corrupted. The `If-None-Match` guard in §3 step 4 detects the
  clash but does not prevent it.
- **One credential per device.** A shared key lets any device delete
  everything. Per-device keys with prefix policies limit the damage, though
  snapshots and `objects/` still need shared write access.
- **Revoking a device** means rotating both the repository key and the IAM
  credentials.

## 7. Prior art

| System | What it shows |
|---|---|
| Joplin (S3, WebDAV and Dropbox sync targets) | A consumer app syncing through passive stores, with conflict copies |
| restic and kopia | Repositories on S3 with client-side locks, pruning and grace periods |
| Delta Lake | Commits by creating the next log file only if it is absent (§4) |
| SlateDB | Fencing stale writers on S3 with conditional writes |
| Automerge and Ink & Switch's local-first work | CRDTs for rich documents |
| SUNDR | Fork consistency on untrusted storage (§6.5) |

## 8. Fit with Lambda

### 8.1 Transport

Lambda has no S3 client today: no `s3` path scheme and no SigV4 signing in
`lambda/` or `lib/`. It needs little.
- **Signing.** The vendored curl 8.10.1 (`mac-deps/curl-8.10.1`) has
  `CURLOPT_AWS_SIGV4`, so requests can be signed without an AWS SDK.
- **Conditional writes** are ordinary HTTP headers (`If-None-Match`,
  `If-Match`) and an ordinary status code (`412`).
- **Placement.** The client would sit behind the one central IO door
  (D7.5.2), with resource IO in `lambda-io` and the raw mechanism in `lib`
  (D7.1.2v2).

### 8.2 Data

- **Ops and snapshots in Mark notation.** They load with `input(…, 'mark')`
  like any other document.
- **Values never alias** (S1.4). A snapshot is an immutable value, which
  matches what the merge and the storage both want.
- **A sync loop is a long-running task.** It fits a document-owned task
  (S13.3.1v2), which the document cancels and joins when it is discarded.

### 8.3 The mail demo

- **Message bodies** are already written once (§5.4.1 of the mail demo).
  They map to `objects/<sha256>` and never conflict.
- **Only local-only state needs sync.** For IMAP the server is already the
  source of truth. The candidates are POP3 flags and folders (SL2), local
  mailboxes, and `accounts.mark` minus secrets.
- **SL1's planned direction is design A's structure.** The mail demo plans
  an append-only change log beside each index, compacted when the mailbox
  opens. A per-device log with compaction is the same thing, so one mechanism
  could serve both.
- **The POP3 fetcher is the one singleton job.** POP3 deletes mail once it
  has been downloaded, so only one device should fetch at a time. A lease
  (§6.3) prevents duplicated fetches. De-duplication by content hash is the
  safety net if two devices fetch anyway.

## 9. Questions for the user

| ID | Question |
|---|---|
| **Q1** | Which stores must be supported: AWS S3 only, or also R2, MinIO, B2, GCS or Azure? Requiring conditional writes rules out some S3-compatible stores. |
| **Q2** | What is synced: mail's local-only state, settings, other demos' data? Is this a general `lambda.sync` package or part of mail only? |
| **Q3** | Merge model: CRDTs throughout (design A), or git-style history with conflict copies (design B)? |
| **Q4** | Is end-to-end encryption required, and how are new devices paired? |
| **Q5** | Polling only, or optional push through S3 Event Notifications? |
| **Q6** | The retention window, and what happens to a returning device's unpublished edits of records that were deleted meanwhile: keep them as a restore, or drop them? |
