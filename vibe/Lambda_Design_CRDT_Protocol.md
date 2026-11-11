# Lambda document and workspace CRDT protocol v1

**Date:** 2026-11-11
**Status:** Implemented experimental document/workspace protocol, including the
2026-11-11 ZIP extension. Qualification is recorded in the linked implementation
plans; deployment remains separate.
**Authority:** Refines the user-approved [data-sync design](Lambda_Design_Data_Sync.md),
§§6–7 and 12.2. This defines Lambda's own wire protocol, with no upstream wire compatibility claim.
**Spec linkage:** S12.1.1v3 (pure core/effectful host), S2.3.1v2 (map order),
S2.5.6 (persistent arrays), S2.6.1v2–S2.6.5 (element content),
S8.2.2v6 (namespace identity), S12.4.1v3/S14.3.1v3 and D9.5.5 (ZIP capture/carriers),
D1.7/D9.3.4 (source distribution/imports),
D11.2.3–D11.2.4 (trusted host grants and network admission).

## 1. Boundary and algorithms

The document protocol implements identity, causal operations, convergence,
versioned serialization, authentication domains, anti-entropy, compact checkpoints,
saved-frontier capture and explicit recovery. P2P DataChannels and periodic HTTP
polling carry the same messages. Network discovery, encryption, filesystem capture,
external format writers, object-store publication and physical deletion are host
integration tasks. A pure function does not perform them.

The selected v1 reference core composes an RGA insertion tree with causal
multi-value registers. It retains insertion anchors, causal coverage and operation
fingerprints after deleting detailed packets. It uses fixed child ownership:
arbitrary reparenting is outside v1. A subtree replacement creates new child IDs;
it does not claim to be an identity-preserving tree move. Workspace directory
moves use the separate placement interpretation in §10 and the data-sync design
§6.3; they do not reparent Mark nodes.

This is an independently implemented reference protocol. The
[RGA verification work](https://arxiv.org/abs/1707.01747), its
[reference implementation](https://github.com/trvedata/crdt-isabelle), and the
[delta-state/dot-store work](https://arxiv.org/abs/1603.01529v1) with the
[authors' implementation](https://github.com/CBaquero/delta-enabled-crdts)
inform its primitives. Their proofs do not automatically cover this composition.
[Yjs state-vector/update APIs](https://docs.yjs.dev/api/document-updates) inform
anti-entropy behavior, rather than the selected encoding or runtime dependency.

## 2. Identity and causal admission

- A document ID is a nonempty string. The authoritative profile is `text`, `mark`
  or `workspace`. A workspace ID identifies one replicated namespace.
- A document epoch is a nonnegative integer. IDs and counters are scoped by
  document/profile/epoch; an epoch change never happens during normal saving.
- An operation ID is `[actor, counter]`. The actor is a nonempty string with at
  most 256 code points. Counters are positive, contiguous per actor, and below
  `9007199254740991`. The host allocates globally fresh actor IDs for new writers.
- `context` is a sorted array of `{actor, counter}` entries with positive counters
  and no duplicate actors. The operation's own actor counter is exactly one less
  than its ID. Every context prefix must be causally closed.
- Lamport `time` is a positive integer in the same bounded numeric domain. It is
  strictly greater than every causal predecessor's time. Replica time equals the
  maximum applied time, or zero before the first operation.
- An owner, child reference, insertion anchor or deletion target must have been
  observed in the operation's context. The root `['',0]` is implicit.
- Missing causal prefixes buffer operations. Duplicate IDs with the same
  fingerprint are idempotent. Conflicting reuse of an ID is an error, including
  after compaction. Malformed input returns an error without changing the caller's
  immutable state.

Persisted/cloned writer state cannot safely resume a reused counter stream.
`store.load` requires a fresh actor and rejects identities already present in
accepted or pending work. A host that supports durable writer leases can add
that stronger ownership contract separately; v1 does not infer a lease from a file.

## 3. Replicated data and operation records

Every operation has exactly these fields:

```text
protocol, document, profile, epoch, id, time, context,
kind, node, slot, key, left, value
```

`protocol` is integer `1`; unused slot/key/left fields are null. The primitives are:

| Kind | Meaning |
|---|---|
| `node` | Create a map, array, element, text or scalar child under an observed owner |
| `insert` | Insert a typed descriptor after an observed RGA anchor, or at the beginning |
| `delete` | Tombstone a nonempty distinct set of observed atoms in one owner/slot |
| `set` | Assign a map field or scalar/document value; overwrite only observed assignments |
| `meta` | Assign element tag or explicit map-key order in a separate causal register |

Slots are `items` for text/array/element content and `keys` for map/attribute
ordering. Sequence order is insertion-tree preorder. Siblings sort by descending
`[Lamport time, operation ID]`; deleting an atom does not delete its descendants.
Text positions count Unicode code points, independently of UTF-8 transfer bytes.

Descriptors distinguish `scalar`, `node`, `absent`, `blob` and workspace events.
A workspace descriptor is `{kind:'workspace',event:...}` and is admitted only as
an immutable root event in the workspace profile (§10). A scalar cannot hide
a container. Node references must point to a child with the same immutable owner.
Concurrent assignments remain alternatives, including deletion versus assignment.
A new assignment whose context observes all alternatives resolves that field.
Tag/order conflicts use the same causal register rule, outside user map fields.

The Mark profile has a permanent `document` root with a multi-value `value`
register. Replacing a scalar root with a container, or vice versa, is an ordinary
replicated edit. Concurrent root replacements remain explicit alternatives.
Other containers retain stable IDs; strings are editable text children. Map
ordering metadata remains separate from unordered map equality (S2.3.1v2).
New map keys not named in an earlier explicit order append in deterministic RGA
order. Explicit reordering resolves observed order alternatives.

The native projection covers maps, arrays, text and scalar values. Elements use
the inspection record `{kind:'element',tag,attributes,children}`. Projection drops
null/empty strings and coalesces adjacent strings/binaries under S2.6, while
replication anchors remain distinct. Native element construction and external
format fidelity are adapter work. Names requiring namespace fidelity are rejected
when the available codec cannot preserve them; they are never reduced silently
to an unqualified local spelling (S8.2.2v6).

## 4. Typed wire encoding

The codec is `lambda-crdt-json-1`. UTF-8 JSON contains only typed tuples, strings
and structural arrays:

```text
["lambda-crdt-json-1", [tag, payload]]
```

Strings use a string payload. Arrays contain recursively encoded items. Maps
contain ordered `[encoded key, encoded value]` pairs, preserving key order and
key type. Native numbers never pass through untyped JSON numbers. Ordinary scalar
payloads are single-item Mark data literals plus exact type tags; sized numeric
constructors restore their lanes. `u64` has a canonical unsigned decimal field
decoded directly with checked unsigned arithmetic, including `2^64-1`.

Portable scalars include null, booleans, integers, finite floats, decimals,
unqualified symbols, strings, binary bytes and datetimes. Nonportable types,
non-reflexive values, lossy name representations, duplicate keys, excessive
nesting and noncanonical encodings fail. Parsing is data parsing; it does not
evaluate Lambda code. Decoding re-encodes and compares the canonical representation.
Depth is checked before JSON parsing as well as while unpacking typed tuples.

SHA-256 of the canonical encoding identifies checkpoint content. An operation
fingerprint hashes a fixed tuple of its fields. This is integrity/identity, not
an authorization decision. The codec version must change for incompatible byte
rules; future compact binary encodings require their own negotiation/version.

## 5. Message grammar and synchronization

Every message has exactly:

```text
protocol: "lambda-crdt", version: 1, codec: "lambda-crdt-json-1",
document, profile, epoch, schema: [name, version], root, kind, body
```

`root` is the immutable root-node descriptor. Schema name/version, profile,
document, codec and version must match. An incompatible epoch triggers explicit
bootstrap negotiation rather than operation installation.

| Kind | Body and behavior |
|---|---|
| `have` | `{clock, floor}`; contiguous applied frontier and compaction floor |
| `updates` | `{clock, operations, more}`; missing standalone operations, with an advertised frontier |
| `transaction` | `{id, base, frontier, operations}`; one authored atomic operation group |
| `checkpoint_required` | `{checkpoint, bytes, frontier}`; missing packet prefixes need state transfer |
| `checkpoint_frame` | `{checkpoint, index, total, text}`; canonical checkpoint text split into bounded frames |
| `bootstrap_required` | `{checkpoint}`; epochs differ and old pending work must be preserved |
| `ack` | `{clock, level:'received'}`; receipt acknowledgment only |

An endpoint uses `have` and `next`, encoding the result with `codec.encode`.
`next` sends causal predecessors first and preserves authored transaction groups
in its relay journal. A receiver uses `accept`, then sends a new HAVE/ACK. Repeat
until both applied frontiers cover the advertised work. Retries do not allocate
new operation IDs. A dropped response or switch between P2P and periodic HTTP
polling uses the same state-vector exchange. The lower-level `reply` API streams
standalone core operations; endpoint users use `next` to preserve transaction
boundaries. Transport routes are not part of operation identity.

The default incremental batch budget is 128 operations and 64 KiB of encoded
UTF-8 bytes, with a 1,024-operation maximum. An atomic group that exceeds the
negotiated byte budget returns an explicit error; it is not split into visible
partial edits. Its immutable complete state can instead be transferred as an
admitted checkpoint. The receiver defaults to an 8 MiB decode/pending-data budget,
64 waiting transactions, 4,096 causally pending operations, two checkpoint
transfers, and 8,192 frames per transfer. A host can choose a different byte budget.
These are reference limits, not throughput targets.

Checkpoint frames may arrive out of order or repeatedly. Count/index/byte
accounting, duplicate-frame consistency, digest verification and complete state
validation precede merging. The host admits the exact checkpoint digest before
allocating its transfer. The last 16 completed digests suppress ordinary frame
retries; eviction affects efficiency only. Transfer cancellation is explicit.

The sender must retain the immutable state whose checkpoint digest it advertised.
`frames(..., expected_digest)` refuses to substitute a newer state. If that state
is unavailable, restart negotiation. Never mix chunks from two checkpoint objects.

## 6. Atomic editing and undo

`local_change` applies editor commands to a private immutable candidate and
returns a new endpoint plus a pinned undo token. A failed command returns no
partial endpoint. Transactions contain complete causal operation ranges and a
digest of document/profile/epoch, base frontier, resulting frontier and operations.
Receivers wait for the transaction's prerequisites, then install its complete
candidate. The relay journal persists its original grouping until checkpoint
coverage replaces it. Pending transactions survive bundle restore.

Undo creates compensating CRDT operations. It removes this transaction's inserted
atoms, restores deleted payloads using new IDs at retained anchors, and restores
fields only when those fields have not subsequently changed. Concurrent unrelated
text insertions survive. Conflicting field undo requires explicit resolution.
Pinned endpoints retain the payloads required by the supported undo window.
An editor consumes an undo token once; repeated invocations are new edits, not
wire retries. Dropping the token ends that undo promise. Whole-file historical
restore is a separate operation and does not rewind the live head.

Stable cursor gaps contain document/epoch, owner/slot, left/right atom IDs and
left/right affinity. They resolve through deleted anchors. Presence uses a
receiver-local lease and monotone per-actor sequence; replaying an old lease
does not extend it. Presence never advances the document frontier or creates
saved history, and is not stored in the durable endpoint bundle.

## 7. Authorship and checkpoint authority

The host controls current grants under D11.2.3–D11.2.4. `auth` verifies Ed25519
proofs using the existing native verification primitive. It accepts grants scoped
to actor, document, profile, epoch and schema, with `write` or `checkpoint`
permission and a 32-byte public key. Signing/key storage and grant distribution
belong to the host. Revocation removes a grant; old accepted state is not silently
rewritten. Newly rejected work remains in the caller's retained branch.

Signed domains are distinct:

| Proof | Signed canonical value |
|---|---|
| Operation | `["lambda-crdt-operation-1", operation fingerprint]` |
| Transaction | `["lambda-crdt-transaction-1", document, profile, epoch, schema, root, body]` |
| Checkpoint | `["lambda-crdt-checkpoint-1", document, profile, epoch, schema, root, checkpoint digest]` |

An operation proof must name the operation's actor. An atomic transaction proof
must belong to the author of every operation in its group. Relays retain the
original proof. A checkpoint signer needs explicit checkpoint permission and
attests the compact state's covered authorship; a hash alone cannot establish it
after payload logs are removed. Host code can cache verified admission by exact
digest. Proofs travel inside the authenticated/encrypted transport or repository
envelope; the pure CRDT state does not store private keys.

`auth.admission(packet, proofs, current_grants)` produces the narrow admission
record consumed by `protocol.accept`. A host may provide equivalent authenticated
admission, but may not infer operation authority merely from a relay's identity.
The wire proof tests pin exact canonical bytes and include valid signatures,
tampering, changed scope, missing grants and missing proofs.

## 8. Compaction, persistence and acknowledgments

Same-epoch compaction removes covered detailed operation packets and deleted
sequence payloads. It retains RGA anchors, deletion sets, live/conflicting fields,
node ownership, causal clocks, operation fingerprints/times/contexts and the
merge context needed by offline peers. State joins do not resurrect erased
payloads or compacted logs. The new checkpoint still merges a peer's pending
branch rather than overwriting it. This is not a bounded-metadata or globally
minimal representation: anchors, receipts and detached node state can grow.

Saved history is separate. `prepare_save` captures an immutable version with its
exact frontier/export and current saved parents. `commit_save` updates only that
captured disk version; later live edits remain dirty. A changed external disk
base rejects the commit while preserving the prepared result. Concurrent saved
heads remain parent-linked; receiving a saved version merges its state without
replacing this device's materialized disk base. Retention protects the current
disk/import bases; calendar selection and physical repository deletion remain
outside the document protocol.

`store.seal` encodes a checksummed endpoint/session bundle, including unsaved live
state, pending transactions, frame progress, journal, histories and recovery
branches. `store.load` validates it and requires a fresh writer actor. The host
atomically persists the bundle; sealing bytes is not a durability acknowledgment.

Receipt, local durability and cloud durability remain independent. ACK messages
only establish receipt of a causally closed frontier already known locally.
`record_durable` consumes a captured validated checkpoint plus host-confirmed
blob availability after actual durable publication. It can acknowledge that older
captured frontier even when live editing has advanced. Receiving a peer packet
does not call it. A cloud promise includes every required blob, not only its
metadata reference. Host object publication must be complete and discoverable
before that promise; no running function's memory is durable evidence.

Small binary values are atomic with a 64 KiB inline limit. Larger attachments
use an indivisible `{kind:'blob',manifest,size,hash,media_type}` descriptor, with
SHA-256 identities and byte length. Concurrent replacements preserve alternatives;
captions remain independently editable. `blob.verify` checks received payloads.
Zero-length blobs retain an explicit descriptor. Lambda represents their empty
binary payload as null (S2.2.4); integrity checking does not establish successful
host receipt or object availability.
Reachability includes conflicting references and retained saved endpoints, with
conservative retention for detached state. Upload, encryption, chunk transfer and
physical collection belong to the repository layer.

## 9. Epoch and external-edit recovery

Same-epoch synchronization uses delta/checkpoint reconciliation. If the epoch is
retired, `bootstrap` requires an admitted newer checkpoint and preserves the old
replica, pending groups and transfer/journal state in a recovery branch before
installing it. Old operations are not automatically reinterpreted in the new
epoch. Recovery uses an explicit user/editor import or resolution with fresh
identities. Schema/profile changes similarly require an explicit migration; a
mismatch never authorizes a lossy automatic conversion.

Open-editor imports continue to use independent base/live/external branches from
the main design §7.8. Normalized input fingerprints are idempotent, but the raw
external input remains available for review. Disjoint changes merge, overlapping
or ambiguous edits retain both branches, and saving is blocked until resolution.
No host filesystem capture/revalidation guarantee is implied by the pure model.

## 10. Workspace transactions and ZIP workspaces

### 10.1 Namespace profile and atomic groups

The `workspace` profile and `["workspace",1]` schema identify a namespace,
independent of the Text/Mark replicas held by its files. The root is an immutable
directory with ID `['',0]`. Each creation operation allocates its entry ID;
names and paths never serve as entry identity. File/directory kind is immutable.

Workspace events are typed `workspace` descriptors in an append-only sequence
under a `workspace` root. They reuse operation IDs, causal admission, signed
transaction domains, framed checkpoints and restart bundles. An event is a
creation, placement, deletion, explicit restore or immutable revision publication.
Only root insertions with no sequence anchor are admitted in this profile.
The application interprets the event set in ascending `[Lamport time, operation
ID]` order. Descriptor payloads remain native typed data, never encoded user
fields or filenames. This profile does not alter ordinary Lambda value semantics.

Event fields use the following construction order. The surrounding operation
supplies the event ID, Lamport time and causal context; those are not duplicated
inside the event descriptor.

| `action` | Event fields after `action` |
|---|---|
| `create` | `type`, `parent`, `name`, `revision`; directories use null revision |
| `place` | `entry`, `parent`, `name` |
| `delete` | `entry` |
| `restore` | `entry`, `deletions`; sorted distinct observed deletion IDs |
| `publish` | `entry`, `revision` |

An immutable revision is `{id, workspace, epoch, file, parents, content, document}`.
Its ID is the codec digest of `["lambda-file-revision-1", body]`, with body fields
constructed in the order `workspace, epoch, file, parents, content, document`.
The document link is null or `{document, profile, epoch, schema, frontier,
checkpoint}`. Content is a blob descriptor (§8); revision parents sort by ID.
The receiver verifies the hash, matching workspace/epoch/file, and observation of
every parent revision. ZIP capture and namespace checkpoint identities remain
separate from immutable file revision identities.

One author can group changes to multiple entries in one existing protocol
transaction. Commands can use transaction-local aliases for newly created
parents. Every replica waits for dependencies and installs the whole group;
retries retain the original IDs. Atomicity covers the replicated namespace.
Bulk ZIP capture/import uses groups bounded by 128 operations and the actual
64 KiB encoded message, reducing group size for long member names or contexts.
Atomic visibility is per group; an archive file version pins the complete saved
frontier. Package consumers can wait for that frontier before opening it.
Host filesystem materialization has a separate crash-recoverable journal and
does not promise simultaneous multi-file visibility on every operating system.

### 10.2 Moves, lifecycle and revisions

Placement is the indivisible tuple `(parent ID, name)`. Interpret placements
using the ordered, cycle-checked replay selected in the main design §6.3.
The initial reference implementation replays the full retained placement set;
this has the same interpretation as inserting a late move and replaying its
suffix. Keep suppressed placements and expose concurrent placement alternatives.
Creation and parent dependencies must be observed, and parents must be directories.
The root cannot be moved or deleted. Names are nonempty components, excluding
`.`/`..`, slash, backslash and NUL; platform-specific mapping remains a host task.

Deletion records an independent tombstone. Restore names the exact observed
deletions it clears. An unseen concurrent deletion continues to hide the entry.
Hidden directories hide their remaining descendants; edits retain their content
without resurrecting the file. Offline creations beneath a deleted directory
remain recoverable and expose a delete/create conflict. A descendant moved outside a concurrently deleted
directory survives with a conflict notice. Duplicate logical names retain distinct
IDs and obtain deterministic, collision-free names in the materialization view.
That view never generates implicit rename operations.

Each file revision contains its workspace ID/epoch, file ID, sorted parent revision IDs, an immutable
blob descriptor and an optional captured Text/Mark/workspace link. Its identity
hashes all those fields. The link contains document/profile/epoch/schema, causal
frontier and checkpoint digest. Publishing a revision does not replay or merge
its live document operations. Concurrent revision heads remain alternatives;
a resolution revision names their parents. A namespace transaction can combine a
move with a new saved-version reference while content transactions keep their
own document scope. Required content is durable before a host publishes the
discoverable reference (main design §5.2).

Same-epoch compaction can discard transport packets but retains every workspace
event, including suppressed placements and old revision records. It does not
promise bounded move history or physical content deletion. Epoch retirement and
offline branches use the existing explicit recovery contract.

### 10.3 ZIP is a workspace backing store

A ZIP archive uses exactly the namespace model above. Loading captures the
existing `fs` archive tree, assigns member IDs and immutable content descriptors,
and records an accepted disk baseline. Native capture/indexing and lazy member
access retain S12.4.1v3/S14.3.1v3; replication capture deliberately reads member
bytes to obtain integrity identities. An empty member is size zero with the
empty-content hash, even though its Lambda binary value is null (S2.2.4).
Directories, empty files and opaque binary members are retained. Nested ZIPs
remain ordinary binary members until explicitly opened as another workspace.

Member changes synchronize through workspace events and referenced content;
compressed byte offsets, CRCs and outer ZIP modification times are not member
identity. An open member can also have its own Text/Mark replica. Saving that
member publishes a revision with its captured document link.

Archive save preparation pins a workspace checkpoint/frontier and produces a
constructed `fs` tree from verified member objects. Missing blobs or unresolved
content heads block export; name collisions use the deterministic view. Existing
ZIP output rebuilds and atomically replaces the outer archive. Commit advances
only the captured baseline, leaving later live edits dirty. An intervening
external baseline change rejects the commit. Native member-name/payload round
trip is required; compressed-byte identity and preservation of every ZIP extra
field are not promised (IO ZIP §7). Package validity remains format-specific.

External replacement imports fork from the accepted archive baseline under a
fresh import actor, then merge into live state. Existing exact-path associations
preserve IDs; an explicitly supplied, validated rename association can preserve
a moved member. Equal bytes alone cannot prove a move. Unknown associations,
file/directory type changes and namespace/content conflicts preserve both inputs
for review. Repeat imports are idempotent by captured-input identity. The pure
model guards baseline changes; the host also checks the outer file immediately
before replacement and journals its own export provenance to prevent import loops.
Control metadata lives in the private repository, not an injected ZIP member.
Captured-input identity covers paths, member kinds, uncompressed sizes and
content hashes, independent of compression, manifest layout and MIME annotations.
Export normalizes ZIP metadata using deterministic output; timestamps, source
compression choices and unknown extra fields are not replicated member edits.

The reference `save` procedure checks the accepted outer-file hash before writing,
uses native atomic ZIP replacement, and verifies the resulting member capture
before committing. A host CAS/locking adapter is still needed to exclude an
uncooperative writer between the final check and replacement. `read_file` captures
raw bytes once before parsing; `refresh_file` imports another captured version.
Checksummed archive restart bundles retain endpoint state, object bytes, accepted
baseline, source evidence and external-review captures. Namespace compensation
uses explicit workspace commands; document undo tokens do not erase workspace
events or promise an automatic filesystem undo.

ZIP read/write remains the existing adapter described in
[IO ZIP](Lambda_IO_Zip.md), under S12.1.1v3, S12.4.1v3, S14.3.1v3 and D9.5.5.
Repository encryption, physical GC, file watchers, transport deployment and
operating-system materialization remain host integration work.

## 11. Implementation and qualification

Source and usage: [sync package](../lmd/package/sync/README.md).
Progress/evidence: [implementation plan](<impl/Lambda_Impl_Sync_Protocol (done).md>).
Workspace/ZIP progress: [workspace implementation plan](<impl/Lambda_Impl_Sync_Workspace (done).md>).
Fixtures use the existing golden harness, with interpreter/AUTO/JIT and forced-GC
qualification. They cover convergence and delivery schedules, malformed state,
typed round trips, signatures, atomic relay/restart, checkpoint framing, saved
frontiers, external branches, attachment availability and epoch preservation.

This protocol does not claim production performance, browser-host qualification,
general Mark subtree moves, native element export, namespace adapter completion,
unlimited undo, repository encryption, network deployment or physical GC.
Those remain separately testable implementation work in the main design.
