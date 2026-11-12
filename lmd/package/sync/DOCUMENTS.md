# Durable live documents and headless editor API

`lambda.sync.document_host` connects the Text/Mark CRDT protocol to the encrypted
local repository, authenticated native peers and periodic cloud fallback. Policy
remains Lambda Script (`fn`/`pn`, S12.1.1v3); repository transactions follow
S12.4.4v4 and native effects pass through D11.1.2. This is the bounded native
profile in [data sync §12.2](../../../vibe/Lambda_Design_Data_Sync.md#122-collaborative-documents).

An edit commits its session before returning. It does not write the working file
or create a file revision. A save captures one CRDT frontier, installs its export
and publishes a linked workspace revision. Later edits remain unsaved. Ordinary
files stay available to other applications.

## Host API

Create/enroll repositories through the [peer agent](PEER.md); open their handles
through `peer_agent.open_agent(directory, wrapping_key)`. The trusted host owns
handles, credentials, scheduling and IO grants (D11.2.1–D11.2.4). Dispatch editor,
peer and polling work sequentially in that owner. The standalone command is for
sequential invocations; it does not add concurrent repository-writer qualification.

| `document_host` operation | Result/behavior |
|---|---|
| `open_document(handle, path, profile='text', encoding='text')` | Capture/scan a known regular file; resume its durable session or initialize deterministic document/seed identities. Return status. |
| `apply_edits(handle, id, commands, resolve=false, request_id=null)` | Apply one atomic protocol command group and commit before reply. With an ID, retries cannot repeat edits; reusing it for different commands fails. |
| `status(handle, id)` | Return projected value/error, file identity/path, frontier, dirty/deleted state, saved version IDs, notices, conflicts, file heads and coverage. |
| `refresh(handle, id)` | Scan disk and import external changes from the materialized disk base. |
| `prepare_save(handle, id, request_id=null)` | Commit an exact-frontier save intent. An already completed save ID returns its recorded version. |
| `install_save(handle, id)` | Perform/read back the intent's guarded disk write; leave the intent ready for finalization. |
| `commit_save(handle, id)` | Resume installation if needed, then commit the saved document and linked file revision. |
| `save(handle, id, request_id=null)` | Prepare and finish a save. Reuse the same ID after an uncertain response to finish/inspect the original save rather than capture another frontier. |
| `cancel_save(handle, id)` | Cancel a pending intent before its export is installed. Installed saves must finish. |
| `compact(handle, id)` | Compact current operation payloads and covered transaction packets in the same epoch; keep anchors, causal receipts, saved endpoints and unresolved imports. |
| `restore_version(handle, id, version_id)` | Create new compensating edits from a retained saved version. Explicitly save afterward to create a new file revision. |
| `attachment(handle, bytes, media_type?)` | Store encrypted, chunked bytes and return a verified immutable blob descriptor. Use a `blob` command to attach it to a Mark field. |

Commands are the existing [atomic editing commands](README.md#editing-and-data).
Provide stable editor request IDs for retries across process restarts. Private
request receipts are retained in this profile. A cancelled request ID should be
abandoned; a new intent gets a new ID. Save completion consumes its pending intent;
after losing a completion reply use `save` with its ID or inspect `status`.

`coverage.local`, `coverage.peer` and `coverage.cloud` are separate. They require
the accepted edit frontier and the current immutable document root. A peer
receipt cannot imply cloud durability. A new save with the same edit clock, or
a newly opened empty text document, still needs its own root acknowledgment.
The agent's top-level `cloud_saved` combines workspace and document coverage;
cloud status with a configuration checks that provider's publication receipts.
Coverage refers to shared accepted editing/saved state; private UI/request state,
save intents and unresolved external-review records remain in the local journal.

## Headless command

Run `./lambda.exe run lmd/package/sync/document_cli.ls` with:

- `LAMBDA_SYNC_REQUEST`: path to a public JSON request;
- `LAMBDA_SYNC_KEY`: device-store wrapping key supplied by the host;
- `LAMBDA_SYNC_RESULT`: optional path for a typed-codec result instead of CLI output.

An open request is:

```json
{"action":"open","directory":"/private/agent","path":"notes.txt","profile":"text","format":"text"}
```

The returned `id` is the stable document identifier. Edit with:

```json
{"action":"edit","directory":"/private/agent","document":"DOCUMENT_ID","edit_id":"editor-event-42","commands":[{"kind":"text","start":0,"count":0,"value":"Hello"}]}
```

The CLI supports `open`, `edit`, `status`, `refresh`, `save`, `prepare_save`,
`install_save`, `commit_save`, `cancel_save`, `compact`, `restore_version`,
`versions`, `attachment`, `attach` and `read_attachment`. Saves accept `save_id`;
`restore_version` takes a hex version ID. `edit.commands_file` can name a typed-codec
array for commands containing binaries/symbols. JSON command `kind` strings are
converted to symbols at this boundary. Attachment commands take an anchored
`root` and relative `path`; `attach` also takes `document` and `key`, and
`read_attachment` installs into a previously absent target.

Call the existing peer `sync`/`serve`/`watch` or cloud `sync`/`watch`/`run` to
replicate accepted edits. The editor integration host can trigger direct exchange
after committing an edit. Scheduling remains explicit; the CLI itself does not
start a hidden service or save on receipt of a live update.

## Formats and external writes

Source editing uses Text CRDT with `format:'text'`, including incomplete syntax.
Structured editing uses one Mark CRDT with `format:'json'`, `'xml'` or `'mark'`.
A tracked file has one persisted editing profile and format; an incompatible
open or independently initialized branch is rejected with existing work retained.
Changing the profile requires an explicit migration outside this first host.
Parsing occurs on initial open or external import; formatting occurs on save.
Native Mark elements are rebuilt from node types with computed element tags
(S16.8.10); ordinary user maps are never recognized by magic field names.
XML keeps the parser's document wrapper. Every export is parsed back and compared
against its editing projection before disk changes, so unrepresentable values,
names or namespace conversions fail without silently losing data. This does not
promise byte-identical whitespace, comments or lexical formatting after an edit.

Typed binary/attachment descriptors round-trip through the Mark format. Ordinary
JSON cannot represent all Mark scalar types: a JSON save that would erase binary
or symbol types is rejected. Formats needing a different attachment notation
require an explicit schema adapter; no reserved user-map-key heuristic is used.
Saved checkpoints retain attachment semantics and editing IDs on normal reopen
and cloud recovery; the raw export alone does not contain the CRDT identity graph.

Disk content matching a known saved version adopts that version's existing IDs.
Other changes fork from the known disk base and merge with live unsaved edits.
Repeated captures are idempotent. Invalid structured input, invalid UTF-8 or
overlapping changes retain raw input and local work for review. `status.notices`
and `conflicts` let the editor show the result; `editing_conflicts` exposes Mark
register alternatives even when the value cannot be projected. `workspace_review`
reports unresolved namespace identity; its raw capture is retained without
guessing a document mapping. `resolve:true` commits explicit
resolution edits. Renames retain the document identity. Deletion keeps recovery
work and prevents an implicit save/resurrection. Review/cancellation never
silently reloads over unsaved edits.

## Replication, storage and bounds

The private encrypted local state holds endpoints, sessions, request receipts,
import bases, review records and save intents. Shared immutable `document`
objects hold compact live checkpoints, the initialization identity and retained
history page references. Document roots remain repository object kind 5; saved
endpoints/pages use new kinds 6/7. Existing kind codes are stable.
[History and maintenance](MAINTENANCE.md) describes bounded listing, timestamps,
retention and explicit hydration of cold saved chunks.
Saved workspace revisions point to both export bytes and their exact document
checkpoint. Current and saved attachment roots must reconstruct before commit/ACK.

Peer offers version 3 include a bounded document catalog. New/mismatched saved
histories transfer an authenticated immutable root through existing fragments.
Peers with the same saved history use `protocol.next` atomic deltas and same-epoch
checkpoint frames after compaction. Device-signed `lambda-document-relay-1`
envelopes bind space, document initialization, packet and sender frontier. The
current authenticated full-space writer grant authorizes this relay attestation;
it is not an original-author-signature relay or a finer document-sharing grant.
Outgoing document state stays pinned during a connection. Each accepted group or
frame commits before reply; callbacks recheck current peer grants.

Cloud bundle version 3 binds document root IDs into the already signed encrypted
publication and its complete immutable dependency inventory. Pull installs and
validates roots before recording document coverage. The function API stays
stateless and polling stays periodic. Readers accept legacy version-1/2 bundles;
older readers require upgrading before joining this profile. Peers without the
document extension require an explicit upgrade for live-document exchange.

The inherited profile limits apply: at most 64 tracked documents, bounded 8 MiB
repository/checkpoint envelopes and file captures, 1,024 operations per atomic
editor group, and bounded peer frames. Saved history uses immutable DAG pages, while live roots and each endpoint must
fit one bounded immutable object. There is no silent truncation. Array/LCS indexes
are the reference algorithms, without a production performance claim.
Same-epoch compaction retains merge anchors; explicit retention can retire
intermediate saved endpoints and reclaim unreachable payloads. Metadata epoch
retirement, browsers, UI bindings, traversal, finer grants/key lifecycle and
hosted/physical-device qualification remain separate work. No native CRDT engine
is added.

Acceptance: [document fixtures](../../../test/lambda/proc/sync_document_repository.ls),
[save races](../../../test/lambda/proc/sync_document_races.ls),
[admission/attachments](../../../test/lambda/proc/sync_document_admission.ls) and
[real-process gate](../../../test/sync_documents/test_documents.py).
The latter uses the [cloud test-store setup](../../../deploy/sync_cloud/README.md)
and `LAMBDA_CLOUD_TEST_STORAGE_FILE`. Detailed qualification is in the
[implementation plan](<../../../vibe/impl/Lambda_Impl_Sync_Documents (done).md>).
`python3 test/sync_documents/test_stress.py` runs focused persistence/export goldens with
every-allocation GC/poison and the existing 300-second stress budget; ordinary
GTest discovery retains its 60-second deadline. The separate batch timeout cleanup
defect remains [LR01-24](../../../vibe/Lambda_Issue_Ledger.md#lr01-24).
`--full` also attempts the large workflow goldens; that forced-GC profile remains
unqualified because admission exceeds the 300-second budget on the recorded host.
Those complete workflows remain in the normal all-tier functional gate.
