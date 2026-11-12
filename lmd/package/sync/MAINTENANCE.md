# Saved history, retention and cache maintenance

The native reference host implements this policy in Lambda Script (S12.1.1v3,
D9.3.4). Repository effects use the existing IO boundary and transactional
ownership rules (D11.1.2, S12.4.4v4). Permanent retention is the default.
Configuring a shorter policy permits selection; deletion requires the separate
maintenance steps below. No language semantics or native APIs change.

## Two history levels

Live Text/Mark CRDT state retains the identities, causal receipts and anchors
needed to merge offline work. Same-epoch compaction removes covered transport
packets. It does not promise a replayable sequence of every keystroke.

Each saved version has an independent compact endpoint, exact serialized bytes,
frontier and saved parents. New saves bind an authored timestamp and unique save
intent into their ID. Workspace file revisions also bind an optional authored
timestamp; legacy records keep their original hashes and use arrival time when
no managed save provides one. Rename/move preserves the file's history group.

`history.ls` stores endpoints as immutable `version` objects and headers in
immutable `history` DAG pages (16 entries per page). Shared document roots and
private persisted sessions hold page references instead of an array of every
saved payload. Legacy roots/sessions expand and migrate on mutation. Ancestry
headers remain after payload retirement, so offline parents remain identifiable.

`document_host.versions(handle,id,cursor=0,limit=32,expected_root=null)` returns
`{root,versions,next}`; the maximum limit is 128. Pass the returned root on later
pages. Retirement changes invalidate that root. The editor CLI accepts `cursor`,
`limit` and hex `root`; its old request without paging fields remains compatible.
`maintenance.file_history` provides corresponding raw-file revision metadata.
An available historical endpoint may have deferred chunks; hydrate before restore.
Restore creates new compensating work and a new save, preserving concurrent work.

## Policy

`retention_policy.permanent` and `retention_policy.daily(zone)` supply defaults.
A complete daily policy in a maintenance CLI request is:

```json
{"action":"configure","directory":"/private/agent",
 "policy":{"version":1,"mode":"buckets","timezone":"America/New_York",
   "keep_all_ms":86400000,"rules":[{"unit":"day","count":null}],
   "arrival_grace_ms":86400000,"delete_grace_ms":86400000,
   "clock_skew_ms":300000},"overrides":[]}
```

Modes are `forever` and `buckets`. Rules select the latest causal representative
per completed calendar bucket. Units are `day`, `week` (Monday start), and
`month`; positive `count` limits that rule's calendar horizon, while null keeps
all such buckets. Multiple rules form a union. For example, 30 daily, 26 weekly
and unlimited monthly buckets form a tiered policy. Empty buckets add nothing.
The configured IANA zone controls calendar/DST boundaries. The current day,
recent-all interval and recent arrivals are protected. Implausibly future
authored times are clamped to arrival; causal descendants win backward-clock ties.

Overrides are `{id:history_group,policy:complete_policy}` entries. Pins accept
saved-version or workspace revision IDs. Unresolved heads, named snapshots,
live/base/conflict state, pending saves/imports, attachment roots, staging and
unpublished recovery work remain protected regardless of calendar selection.
Linked managed-save/file endpoints retire together. Selection can retain more
than one payload per day because those protections take precedence.

## Local maintenance

Run `maintenance_cli.ls` with the same `LAMBDA_SYNC_REQUEST`, private
`LAMBDA_SYNC_KEY` and optional result-file convention as [the peer CLI](PEER.md).
Hex IDs in CLI requests correspond to binary IDs in script APIs.

| Action | Fields and effect |
|---|---|
| `configure` | `policy`, optional `overrides`; invalidate stale plans |
| `pin` | `ids`, `enabled`; change explicit retention pins |
| `headers`, `file_history` | Inspect history; the latter takes `entry`, optional `cursor`, `limit`, `root` |
| `plan` | Optional `at` in Unix milliseconds; return candidate IDs, fingerprint and grace deadline |
| `forget` | `id`, optional `at`; validate the exact plan and publish retirement metadata |
| `plan_prune` | Capture currently unreachable local objects in a durable bounded plan |
| `prune_step` | `id`, optional `budget` (default 16, maximum 128); revalidate roots and resume |
| `snapshot`, `release_snapshot` | `label`, or `snapshot`; named snapshots otherwise remain roots |
| `release_recovery` | `ids`; explicitly release inspected orphan/recovery roots |
| `restore_file` | `entry`, `version`; restore retained content as new work |
| `verify`, `scan` | Verify repository integrity or reconcile external filesystem changes |

Retiring references and deleting objects are distinct. Retirement manifests
replicate monotonically over P2P/cloud; returning old bundles cannot resurrect
expired history or released shared snapshots. A returning device's private copy
of an old snapshot becomes a recovery pin. Unindexed authenticated objects are
also recovery roots; rebuilding locators preserves that quarantine.

Every physical delete step validates the current root fingerprint. A real
uncommitted head write acquires SQLite's writer lock before deleting files;
entering a deferred SQL scope alone does not acquire that lock. Progress commits
after deletion. A process crash rolls back the cursor, and retrying deletion is
idempotent. This follows SQLite's [transaction ownership rules](https://www.sqlite.org/lang_transaction.html).
Power-loss durability retains the local repository's existing qualification limits.

## Cloud maintenance and scheduling

Every publication uses one conditional per-space catalog. The owner pulls all
admitted heads, selects retained roots and copies them into a fresh physical
storage epoch. A signed cut covers the frozen heads and all cache reservations.
Only that cut permits bounded deletion of old-epoch keys. Delayed uploads and
deletes can name old keys but cannot affect the new epoch. Immutable logical IDs
remain unchanged. Old-key reaping can repeat after a delayed PUT recreates garbage.

`cloud_cli.ls` supplies `maintenance_prepare`, `maintenance_step` (`budget`),
`maintenance_status`, `maintenance_abort`, `maintenance_prune` and
`maintenance_reap` (`id`, `budget`, maximum 64). Copy progress and cut intent
survive native client exits and lost responses. Changed heads, policy, pins or
roots require replanning. Active upload reservations block a new cut until
their writer completes or explicitly calls `cancel_publication`; no lease
expiry guesses that an unavailable writer has finished.

For periodic automation, invoke `maintenance_tick` from the owner's OS scheduler
with the normal cloud configuration. Each call advances one bounded phase:
selection/grace, copy/cut, cloud pruning, local pruning. Optional `options` is
`{enabled:true,interval_ms:3600000,budget:32}`. Persisted progress is provider-bound.
Ordinary sync/watch does not implicitly enable destructive maintenance. With no
key holder online, encrypted selection waits. A trusted private server scheduler
can call `cloud_function.maintain(store,space,budget=32,reap=false)` to execute
an already committed deletion plan without repository keys. Do not expose that
private entry as an unauthenticated public operation.

Upgrade every client and drain old handler invocations before enabling this
protocol. Legacy stores with untracked uploads require an audited inventory
before destructive maintenance. Missing catalogs fail closed. Each logical
publication/cut saves an immutable `catalogs/<digest>` backup; pull returns its
ID and clients persist it per provider. Owner-only `catalog_recover` takes an
explicit `snapshot` digest, validates signatures and existing dependencies, and
restores a missing pointer. It keeps destructive maintenance disabled pending an
inventory audit. Automated latest-backup discovery and that audit tool are not
implemented. A backup whose required bytes were already purged cannot restore
them. Keep service metadata and backup references with disaster-recovery records.

## Independent local cache

`cloud_cli` actions `cache_plan` (`limit`, maximum 128), `cache_step` (`id`,
`budget`), `cache_hydrate` (`budget`) and `cache_cancel` manage local cold chunks.
They leave shared retention unchanged. Planning reserves copies at the service
and downloads/authenticates actual ciphertext before recording verified intent.
Each removal rechecks live/base/staging/recovery/snapshot roots under the local
writer fence. Metadata always remains local. A crash can reconstruct deferred
availability from the durable verified intent.

`verify` distinguishes authenticated deferred chunks from damage and reports
`data_complete:false`. Hydration validates current-epoch bytes and restores them
locally before releasing the owner's remote reservation. Cancel hydrates any
removed prefix. New peers can receive deferred historical availability and later
hydrate it; ordinary live files and pending edits require local bytes. Full-space
writer/checkpoint trust remains the existing profile (D11.2.1–D11.2.4).

## Bounds

This is a bounded reference implementation. Envelopes/captures remain 8 MiB;
cloud bundles contain at most 8,191 repository objects. Catalog key tracking is
bounded to 32,768, local prune candidates to 8,192 and history traversal to 65,536
pages. Ancestry, arrival and retirement metadata remain retained under existing
envelope bounds; this phase does not implement metadata epoch retirement or
unlimited histories. Cloud epoch replacement copies retained bytes again;
packing, throughput/cost tuning, fine sharing/key rotation, browser/GUI wiring
and hosted deployment qualification remain separate work.

Implementation, exact qualification and residue:
[completed retention milestone](<../../../vibe/impl/Lambda_Impl_Sync_Retention (done).md>).
