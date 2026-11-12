# Lambda Script document and workspace replication

An experimental, transport-independent CRDT protocol implemented in pure Lambda
Script. It supports Text and Mark-oriented documents, bounded delta exchange,
authenticated checkpoint recovery, atomic editing, saved history and restart.
Workspace transactions add stable file/directory identities, cycle-safe moves,
saved revision heads and ZIP-backed namespaces.
The [v1 protocol contract](../../../vibe/Lambda_Design_CRDT_Protocol.md) defines
its formats, invariants and limits; the broader
[data-sync design](../../../vibe/Lambda_Design_Data_Sync.md) defines integration.

The algorithms run in `fn` code (S12.1.1v3). Existing native primitives supply
hashing and signature verification. Transport and editor integration belong to
effectful host adapters. The [native local agent](LOCAL_REPOSITORY.md) implements
encrypted immutable repositories, filesystem reconciliation, installation
journals and recovery in Lambda Script, using narrow native crypto/disk APIs.
The [authenticated native peer adapter](PEER.md) connects independent agents on
LAN/reachable IPv4 endpoints, with encrypted enrollment, signed device sessions,
missing-object exchange, signed workspace checkpoints and periodic retry.
The [cloud adapter](CLOUD.md) adds immutable object-storage publication, a stateless
HTTP function, periodic fallback, durable coverage and cloud-only snapshot recovery.
The [durable document host](DOCUMENTS.md) adds persistent unsaved sessions,
incremental authenticated document exchange, exact-frontier file saves and a
headless editor API.
Package distribution/imports follow D1.7 and D9.3.4.

## Run the examples

```sh
./lambda.exe test/lambda/sync_protocol_example.ls
./lambda.exe test/lambda/sync_example.ls
./lambda.exe test/lambda/sync_workspace_example.ls
./lambda.exe run test/lambda/proc/sync_local_filesystem.ls
```

The first demonstrates a direct transaction followed by offline checkpoint
recovery. The second preserves unsaved editor changes while importing an external
save from the known disk base.

## Endpoint API

```lambda
import text: lambda.sync.text
import protocol: lambda.sync.protocol
import codec: lambda.sync.codec

let base = text.from_text("document", "seed", "Hello")^
let alice = protocol.local_change(protocol.start(base)^, "alice",
    [{kind:'text', start:5, count:0, value:"!"}])^.endpoint
let bob = protocol.start(base)^
let message = protocol.next(alice, protocol.have(bob.replica)^)^
// the host has already authenticated the original author for these exact operations
let received = protocol.accept(bob, codec.encode(message)^,
    {actors:["alice"], checkpoints:[]})^;
text.read(received.replica)^
```

Result: `Hello!`. Real host code can use `auth.admission(message, proofs, grants)`
to verify original Ed25519 authorship and obtain the admission record. Never
construct that record solely from an unverified relay's claimed actor IDs.
Checkpoint admission names the exact authorized digest.

Exchange HAVE/`next` messages until both applied frontiers cover the advertised
work. Keep original transaction proofs while relaying. A missing compacted prefix
returns `checkpoint_required`; pin its immutable state and send `frames` for the
advertised digest. The receiver merges its pending work with the complete
validated checkpoint. P2P and periodic HTTP polling use identical messages.
`reply` is the lower-level standalone-operation API; use endpoint `next` to
preserve atomic groups on recovery and relay paths.

## Modules

| Module | Principal operations |
|---|---|
| `lambda.sync.core` | Causal receive/join, operation receipts, RGA/register primitives, `validate`, `compact`, `snapshot`, `restore` |
| `lambda.sync.schema` | Shared structural, identity, clock and descriptor checks |
| `lambda.sync.codec` | Canonical versioned typed `encode`, `decode`, `digest` |
| `lambda.sync.auth` | Signed operation/transaction/checkpoint domains and grant-based `admission` |
| `lambda.sync.protocol` | `start`, `have`, `next`, `accept`, checkpoint `frames`, ACK, `record_durable`, `bootstrap`, endpoint validation |
| `lambda.sync.sequence` | RGA adapters and LCS correspondence |
| `lambda.sync.text` | Unicode `read`, `insert`, `remove`, `splice`, `replace`, guarded external `reconcile` |
| `lambda.sync.mark` | Stable containers, fields, child text, root replacement, tag/order edits, blob fields and explicit conflict resolution |
| `lambda.sync.document_format` | Text/JSON/XML/Mark open/save conversion and checked native-element export |
| `lambda.sync.document_state` | Shared document snapshots, private session boundaries, request receipts and root-aware coverage |
| `lambda.sync.documents` | Transactional document roots, attachment dependencies and disk-base refresh |
| `lambda.sync.document_host` | Durable editor open/edit/status/save, external imports, retained-version restore and compaction |
| `lambda.sync.document_exchange` | Bounded catalogs, signed relay deltas, checkpoint recovery and document acknowledgments |
| `lambda.sync.document_cli` | Headless editor command; see [API and limits](DOCUMENTS.md) |
| `lambda.sync.cli` | Common public-request/private-key command envelope |
| `lambda.sync.change` | Atomic command execution, operation groups, pinned compensating undo |
| `lambda.sync.position` | Stable cursor gaps and affinity through deleted anchors |
| `lambda.sync.presence` | Receiver-local leases with replay-resistant counters |
| `lambda.sync.blob` | Immutable descriptors, integrity checks, availability and retained roots |
| `lambda.sync.session` | External imports, exact-frontier save preparation/commit, saved parents, conflict/dirty state and retention selection |
| `lambda.sync.store` | Validated endpoint/session persistence bundles and fresh-writer restore |
| `lambda.sync.workspace` | `start`, `transact`, `have`, `read`, `entry`, `path`, `content`, `payload`, `document`, retained `roots` |
| `lambda.sync.workspace_commands` | Lower namespace commands and transaction-local creation aliases |
| `lambda.sync.workspace_rules` | Validate events, observed identities, revisions and document links |
| `lambda.sync.workspace_tree` | Ordered placement replay, tombstones, revision heads and deterministic conflict views |
| `lambda.sync.workspace_zip` | `capture`, `from_archive`, `read_file`, `update`, save preparation/commit, `refresh_file`, external imports/review and restart |
| `lambda.sync.peer_identity` | Signed device descriptors, encrypted invitations, credential scopes, local trust/revocation |
| `lambda.sync.peer_session` | Signed ephemeral handshake and direction/sequence-bound encrypted records |
| `lambda.sync.peer_exchange` | Pinned signed checkpoints, inventory pages, object fragments and admission |
| `lambda.sync.peer` | Native listener, mutually authenticated connections, requests and bidirectional exchange |
| `lambda.sync.peer_agent` | Device/space setup, enrollment, current-grant checks, serve/sync/watch and status |
| `lambda.sync.peer_cli` | Runnable JSON-request CLI; see [peer setup and limits](PEER.md) |
| `lambda.sync.s3` | Provider-specific SigV4, conditional writes and scoped expiring capabilities |
| `lambda.sync.cloud_schema` | Signed owner control, request/publication validation and dependency commitments |
| `lambda.sync.cloud_function` | Stateless authorization, receipts, conditional publication and membership |
| `lambda.sync.cloud_transport` | Signed finite HTTP requests and direct ciphertext transfers |
| `lambda.sync.cloud_bundle` | Encrypted merge-safe workspace/document bundles and checkpoint import |
| `lambda.sync.cloud` | Pull/publish, provider coverage, immutable publication history and recovery |
| `lambda.sync.cloud_agent` | P2P-first scheduling, combined native owner and periodic HTTP fallback |
| `lambda.sync.cloud_cli`, `lambda.sync.cloud_function_cli` | Client/operator and one-invocation function process entries |

Pure APIs return immutable values. The ZIP `save` procedure performs explicit
filesystem output. Fallible calls use ordinary errors and postfix
`^`. A failed operation leaves the supplied state available for recovery.

## Editing and data

Actors own contiguous counter streams. Allocate a fresh actor after cloning or
restoring obsolete state; `store.load` enforces freshness against known work.
Operation fingerprints detect conflicting reuse even after compaction. RGA
sequence order and causal multi-value joins converge independently of packet
order. Concurrent field, root, tag, order and blob replacements retain explicit
alternatives rather than selecting a timestamp winner.

The Mark profile has a permanent document-value register, so root types can
change. Use `content_node` to get its current editable child. Maps, arrays,
elements and text children have stable IDs; user fields contain no sync metadata.
Map order is significant to projection/dirty detection while native map equality
remains unordered (S2.3.1v2). Element projection follows S2.6 content normalization.

Native maps/arrays/text/scalars project directly. An element projects to the
inspection record `{kind:'element', tag, attributes, children}`; external writers
and native-element reconstruction are separate. Names requiring an unavailable
lossless namespace adapter fail explicitly (S8.2.2v6; existing
[LR03-40](../../../vibe/Lambda_Issue_Ledger.md#lr03-40)).

`local_change` supports text/child splices, field set/remove/resolve, scalar/root
replacement, tags, key order and blob fields. Transactions install atomically
when their prerequisites arrive, survive restart, and retain relay grouping
until checkpoint coverage. Undo creates new compensating edits and retains
concurrent unrelated text insertions. Field changes after the transaction need
explicit resolution; the editor consumes each pinned undo token once.

Small binary values are atomic, with a 64 KiB inline limit. Larger attachments
use indivisible manifest/size/hash/media-type descriptors. Blob alternatives,
retained versions and detached recovery state retain their content roots.
Availability checks do not upload or encrypt bytes.

## Persistence, saves and external edits

`store.seal` returns bytes for a validated checksummed bundle; the host must store
them atomically. `store.load` restores unsaved live state, pending messages,
checkpoint progress, saved history and recovery branches with a fresh writer.

Receipt, local durability and cloud durability are separate. Receiving data never
marks it cloud-durable. After verified publication, `record_durable` records a
captured frontier and required blob availability. Newer live edits stay pending.
Signing, grant distribution, durable writes and publication evidence are host
responsibilities under D11.2.3–D11.2.4.

`prepare_save` captures export/state at one frontier. `commit_save` advances the
disk version without absorbing later edits, and rejects an intervening disk-base
change. Received saved heads retain their parentage. `retain` selects logical
endpoints while protecting active bases; daily policy and physical deletion are
repository maintenance work.

External changes fork from the accepted disk base under a fresh import actor,
not from the unsaved live buffer. Disjoint changes merge with a notice; overlap,
unknown bases, parsing failures and ambiguous identity mapping preserve raw input
and unsaved work for review. Successful external saves advance the disk base to
the external branch. No capture/save race guarantee is implied without a host
adapter. Repeated import IDs remain idempotent after compaction.

## Workspace transactions

`workspace.start(id)` creates a namespace endpoint with profile `workspace` and
schema `["workspace",1]`. `transact(endpoint, actor, commands)` uses the same
signed atomic-group protocol as document editing. Use `workspace.have(endpoint)`
for negotiation, then `protocol.next`/`accept`. Pass `workspace.model` to lower-level
protocol functions whose default schema is the document model.

| Command kind | Required fields after `kind` | Optional fields |
|---|---|---|
| `create` | `type:'file'|'dir'`, `parent`, `name`; files also need `content` | `as` transaction-local alias; `document` checkpoint link |
| `move` | `entry` | `parent`, `name`; omitted components retain current placement |
| `delete` | `entry` | — |
| `restore` | `entry` | `deletions`; defaults to observed active tombstones |
| `publish` | `entry`, `content` | `parents`; defaults to observed revision heads; `document` |

References are stable `[actor,counter]` entry IDs or aliases declared by an earlier
creation in the same group. The root is `workspace.root`. A file's initial
revision and its identity are allocated together. Revision hashes include the
workspace ID and epoch as well as the file ID. Content references come from
`workspace.content(bytes)` or an immutable repository adapter. Keep the returned
object bytes separately; metadata receipt does not transfer them. The
[workspace example](../../../test/lambda/sync_workspace_example.ls) creates a
directory, moves a file and publishes its saved version in one transaction.

`read(replica)` returns entries and explicit conflicts. Listings expose one
cycle-free placement, retained content heads and tombstones, plus generated
conflict names that avoid real/generated name collisions. `path(..., true)` uses
those materialization names. Logical names stay unchanged. `roots` includes
historical content references and linked document/workspace checkpoints; durability
evidence must cover both. `document(saved_checkpoint)` builds a captured link.

Every workspace event survives same-epoch compaction. Full sorted placement
replay supports late offline operations and reconsideration of suppressed moves.
It is a reference implementation, with no bounded-history or performance claim.
Namespace compensation uses explicit commands, rather than document undo tokens.

## ZIP workspaces

`workspace_zip.from_archive(id, actor, input(path,'zip')^)` imports a captured
archive. `read_file(id, actor, path)` additionally captures the outer bytes once
before parsing and remembers their hash for guarded saves. Nested archives remain
binary members until explicitly opened. Empty directories/files and opaque
binary payloads are retained. The initial capture limits are 10,000 entries and
64 MiB uncompressed payload; existing native archive quotas apply as well. Bulk
imports form groups bounded by 128 operations and the actual 64 KiB wire image,
including long names and causal context.

Apply workspace transactions to `session.endpoint`, then call
`workspace_zip.update(session, endpoint, new_objects)`. `prepare_save` returns a
pinned baseline and constructed `fs` tree. A host can write that tree using
`output(..., {format:'zip', deterministic:true})`, then call `commit_save` after
successful, verified publication. Later edits remain dirty. The `save` procedure
performs the read/check/atomic-write/readback sequence itself. A new target must
be absent; replacing an existing Save As target requires its explicit expected
hash. Host CAS/locking is required to exclude the final external-write race.

`refresh_file(session, import_id, fresh_actor, hints)` imports a new capture from
the known source. `import_external` accepts a separately captured archive and base
ID. Rename hints are `{entry, path}` records backed by host/user identity evidence;
directory hints cover descendants. Equal bytes alone do not establish a rename.
Without evidence, path additions/removals retain separate histories. Type changes
and ambiguous associations retain captured input for review; `resolve_review`
records an explicit resolution after the caller has applied chosen commands.

`seal`/`load` persist the archive session, endpoint, object bytes and pending
review captures in a checksummed bundle. `load` requires a fresh actor. These
reference bundles use the codec's 8 MiB decode limit; large production blobs
belong in the separately journaled local object repository. ZIP output normalizes
container metadata and compression, preserving member names/payload bytes rather
than original compressed representation or every extra field. This package does
not add a hidden synchronization member to user archives.

## Qualification and limits

`test/lambda/sync_*.ls` fixtures cover delivery/merge laws, typed scalar boundaries,
canonical encoding, signatures, malformed input, transaction relay/restart,
checkpoint framing, offline recovery, saved frontiers, external edits and blobs.
Workspace fixtures add move replay schedules, concurrent deletion/restore,
revision heads, signed namespace groups, ZIP round trips/external merges,
restart and long-name batch sizing.
Exact verification evidence is recorded in the
[implementation plan](<../../../vibe/impl/Lambda_Impl_Sync_Protocol (done).md>).
Workspace/ZIP evidence is in the
[workspace plan](<../../../vibe/impl/Lambda_Impl_Sync_Workspace (done).md>).

Same-epoch compaction removes detailed packets and deleted payloads while keeping
anchors, causality, conflict state and counter-fork evidence. Metadata and detached
nodes can grow. Epoch retirement preserves old pending branches for explicit
recovery; it does not silently replay them into a new schema.

This reference implementation uses array-based indexes and quadratic LCS.
Arbitrary Mark subtree reparenting, rich-text span intent beyond explicit Mark trees,
namespace adapter completion, browser execution, physical GC, UI bindings,
Internet traversal and hosted-cloud qualification remain separate work. The
[native document host](DOCUMENTS.md) supplies headless editor integration. The local repository stores
encrypted objects and retains history permanently; its limits and host qualification
are documented [separately](LOCAL_REPOSITORY.md). Native ZIP
capture/output is reused through its existing public API. No native
CRDT runtime, upstream wire compatibility, transferred formal proof, or production
performance claim is implied.
