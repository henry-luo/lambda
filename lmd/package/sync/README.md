# Lambda Script replication prototype

Experimental, pure Lambda Script modules for the
[data-sync design](../../../vibe/Lambda_Design_Data_Sync.md).
This is an in-memory replication/session model. It is not connected to the
editor application, filesystem, peer transport or object storage.

The core uses RGA sequences with retained deletion anchors and causal multi-value
registers. Text and Mark containers share these primitives. The algorithms run
in `fn` code under S12.1.1v3; local variables are private implementation state.
Native services supply SHA-256 fingerprints, not the merge algorithm. Package
imports/source distribution follow D9.3.4 and D1.7.

## Try it

From the repository root:

```sh
./lambda.exe test/lambda/sync_example.ls
```

The example starts with `Hello world` on disk. Lambda inserts `!` without
saving, an external editor saves `Hello world?`, and the merged live text becomes
`Hello! world?`. The external save remains the disk version until a modeled save.
An older peer then merges after detailed update packets have been compacted.

```lambda
import core: lambda.sync.core
import text: lambda.sync.text
import session: lambda.sync.session

let original = session.start(text.from_text("doc", "seed", "Hello world")^)^
let local = session.update(original,
    text.insert(original.live, "alice", 5, "!")^)^
let merged = session.import_external(local, "disk-1", "external-1",
    original.base.id, "Hello world?", "Hello world?")^;
[text.read(merged.live)^, merged.disk, session.dirty(merged)^]
```

Result: `["Hello! world?", "Hello world?", true]`.

## Modules

| Module | Principal operations |
|---|---|
| `lambda.sync.core` | `receive`, `merge`, `packet`, `receive_packet`, `compact`, `snapshot`, `restore` |
| `lambda.sync.sequence` | Shared RGA insertion/removal and LCS correspondence |
| `lambda.sync.text` | `from_text`, `read`, `insert`, `remove`, `splice`, `replace`, external `reconcile` |
| `lambda.sync.mark` | `from_value`, `value`, `value_at`, `child`, `set`, `remove_field`, `splice`, `reconcile`, `conflicts` |
| `lambda.sync.session` | `start`, `update`, `import_external`, `dirty`, `save`, `resolve`, `deleted`, `compact`, `retain` |

All operations return new values. Callers retain older states when they need
snapshot history. Public fallible calls use ordinary error outcomes and postfix
`^`; errors do not mutate the supplied state.

## Replication contract

- A document actor owns a contiguous counter stream. Actor IDs are explicit
  nonempty strings supplied by the caller, with fresh identities after a clone
  or restoring an obsolete writer state. The prototype does not enroll devices
  or allocate durable actor identities for a host.
- Operation identity is `(actor, counter)`; Lamport time orders RGA siblings.
  Receivers buffer missing dependencies and reject conflicting reuse of an ID.
  SHA-256 operation receipts preserve that detection after packet compaction.
- State joins use immutable RGA atoms, deletion sets and causal dot-store joins
  for field assignments. A delayed assignment removes only values in its own
  observed context. Concurrent field replacements remain explicit alternatives;
  `mark.value` reports an error until those fields are resolved.
- `packet(sender, peer)` returns missing incremental operations when available,
  otherwise a compact checkpoint. Checkpoint receipt merges the old peer's
  pending edits with the current state. Incompatible documents/profiles/epochs
  are rejected; retiring an epoch and rebasing its pending work is not modeled.
- `compact` drops covered packet logs and deleted sequence payloads, retaining
  their IDs/order anchors, causal coverage and receipts. It does not collect RGA
  anchors or unreachable structured nodes, and metadata can grow with editing.
  Saved snapshots retain the exact endpoint states they capture.

The packet/snapshot records and formatter-derived fingerprints are experimental
representations, not a canonical production wire/repository format. Receipts
detect counter forks in these tests; they are not signatures, grants, checkpoint
authorization or a hostile-input security boundary.

## Mark model

The prototype constructs stable map, array, element and editable-text nodes.
Maps/attributes have ordered key sequences and multi-value fields; arrays and
element children have RGA item sequences. Child ownership is fixed at creation.
Primitive values, including small binaries, are atomic field/sequence values.
Map order remains distinct from unordered map equality (S2.3.1v2).

`mark.value` projects native maps, arrays, strings and scalar values. An element
projects to the inspection record `{kind:'element', tag, attributes, children}`;
this is not an external format writer or a native-element reconstruction API.
Element content projection drops null/empty strings and merges adjacent strings
and binaries under S2.6.1v2–S2.6.5, while replication identities stay separate.
An ordinary array keeps its null and empty-string entries.

External differences preserve known map fields and text identities. LCS
correspondence detects multiple optimal matched identity sets; ambiguous text or
structured sequences require review. Changed container sequences without a
reliable correspondence also require review. Quadratic LCS and array-based
indexes are small-input reference choices, not performance claims.

Rich-text span semantics, arbitrary subtree moves, tag replacement, schema
migration, exact external-format round trips and browser hosting are not
qualified. Initial/new map-key order is retained; explicit reordering of existing
map keys is not an editing operation in this model. Namespaced XML/Mark fidelity
remains gated by S8.2.2v6 /
[LR03-40](../../../vibe/Lambda_Issue_Ledger.md#lr03-40).

## External changes and history

`import_external(session, id, actor, base_id, raw, parsed)` accepts a host's
captured bytes plus parsed data, or a parse error in `parsed`. Capture and parse
are outside this pure model. The host must establish their relationship to the
known materialized base; ordinary external editors do not provide causal tokens.
An unknown/stale base or failed/ambiguous parse retains the input for review.

The import uses a fresh actor and forks from the accepted disk checkpoint,
not the live editor buffer. Publishing it later does not imply that it observed
local unsaved edits. Repeated import IDs are idempotent; reusing an ID with
different input is rejected. Successful imports advance the disk base to the
external branch, while the merged live document can remain dirty.

Overlapping text replacements, multi-value fields and deletion of a child edited
on the other branch retain both inputs with a review notice. Disjoint changes
merge with a non-blocking notice when the editor was dirty. `save` refuses an
unresolved review/deletion, and `resolve` requires an explicit state containing
the observed causal context. No filesystem operation is performed by either.

`save` creates a self-contained immutable snapshot and modeled disk version.
`retain(session, ids)` demonstrates selecting saved endpoints, preserving the
current disk base and conflict bases. Calendar/time-zone policy evaluation,
physical reachability collection and fenced deletion are not implemented.
Completed external imports can reduce to idempotence receipts during session
compaction; unresolved inputs remain intact. Thus thinning saved versions need
not retain every external file payload or every keystroke packet.

## Evidence and limits

The existing Lambda golden harness discovers `test/lambda/sync_*.ls`; every
fixture has a `.txt` expected result. Fixtures cover merge algebra, 120 delivery
orders, 225 text replacement pairs, duplicates, causal buffering, Unicode,
field/delete conflicts, external imports, saved endpoints, serialized recovery,
counter forks and an old peer returning after compaction. Run with
`--tier=interp`, `--tier=auto` and `--tier=jit`.

Implementation checkpoint and exact verification results are in the
[implementation record](<../../../vibe/impl/Lambda_Impl_Sync_Prototype (done).md>).
No network, storage durability, encryption, directory-move implementation,
device authentication, host save-race guarantee, automatic calendar maintenance
or UI integration is implied by these model checks.

Algorithm references:

- [Gomes et al., Verifying Strong Eventual Consistency, 2017](https://arxiv.org/abs/1707.01747),
  with the [RGA reference/proofs](https://github.com/trvedata/crdt-isabelle).
- [Almeida et al., Delta State Replicated Data Types, v1](https://arxiv.org/abs/1603.01529v1),
  with the [authors' dot-kernel/MV-register reference](https://github.com/CBaquero/delta-enabled-crdts).

This independently composed Lambda prototype does not inherit those proofs or
claim their binary protocol compatibility.
