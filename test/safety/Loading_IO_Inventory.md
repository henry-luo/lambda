# Lambda source and input acquisition inventory

Authority: **D7.5.2, D7.5.5**, pure resolution **S2.4.4 / S16.9.8**,
context ownership **D5.4.2–D5.4.4**, and immutable cache ownership
**D8.5.1v7**. Surveyed against this working tree on 2026-11-03.

This inventory covers Lambda source loading, ordinary input, path acquisition
and task reads with an explicitly bound client.
It is not an inventory of every runtime I/O path or proof of OS containment.
Ordinary CLI entrypoints still lack production admission/client installation.

| Entry / owner | Bound-client acquisition | Ambient mechanism retained for trusted callers |
|---|---|---|
| `runner.cpp::load_script`, file-backed entry or import | Source-authorized canonical identity, then `InputScriptRequest.io` before runtime/process cache lookup. Inline caller-supplied bytes remain explicit snapshots and inherit this context's I/O client. | `file_realpath` only when no client is installed. |
| `input-script-cache.cpp::input_script_cache_acquire_file` | `security_io_read_bytes(..., source=true)` before a hit or miss. | NULL-client acquisition uses `lib/file` bounded regular-file reads. |
| `input_script_cache_copy_file_source` | Same source acquisition seam; denial returns no copied bytes. | Same explicitly ambient NULL-client mechanism. |
| `input_script_cache_refresh_file_unit` | Same seam on every scoped refresh. Permission denial leaves other principals' immutable cache records intact. | Existing invalidation on an unavailable ambient file remains. |
| `runner.cpp::lambda_cache_dependencies_state` | Re-reads each dependency and compares the exact retained Script bytes before same-runtime hits or AST/MIR graph cloning. This also works for private/cache-off images without an immutable template. Changed bytes retire the affected cone; denied bytes reject this load. | Cheap `stat` detector and exact-byte refresh remain for unbound runtimes. |
| `runner.cpp::runtime_loaded_script_get_current`, `capture_script_file_stat` | No native stat probe for a bound runtime; actual source acquisition and dependency refresh establish freshness. | `stat` snapshots only in the unbound branch. |
| `build_ast.cpp::resolve_import` | Pure path construction, then `load_script`; no native existence probe and no ambient JS fallback in this initial Lambda-only client profile. | `file_exists` and the established JS fallback remain for unbound hosts. |
| `module_ast_prebuild.cpp` | A context-owned scope copies the client, isolates its futures and cross-language profile table, and joins its own jobs before releasing the borrowed transport. Root/discovery reads use the same source seam; authorized builder callbacks install the client in the source-only Runtime. | Scope-less profiles retain native canonicalization/reads; they never supply a scoped profile's fallback. |
| `module_registry.cpp::module_registry_key_dup` | Lexical key construction only; the loader supplies the broker's canonical source name. Explicit Runtime ownership is used even before TLS activation. | `file_realpath` remains only for a Runtime without a client. |
| `file_broker.cpp::security_file_canonical_source` | Source permission precedes opening a regular object. A held descriptor supplies its kernel path; named-object revision, root binding, protection and authority are rechecked before returning it. | Host-only `FileAnchor`/descriptor mechanisms in `lib/file.*`. No descriptor is returned to a worker. |

Canonicalization is an authorized acquisition operation. It does not grant
subsequent reads: the byte request must pass the same policy again. Descriptor
paths preserve the host's existing case-alias identity without following an
untrusted path inside the worker. A selected loose file accepts its admitted
and canonical leaf spellings while retaining its object/version check; it gains
no sibling permission. Unsupported native descriptor-path providers fail closed.

Prebuild futures are scheduling hints, not grants. Each actual I/O dispatch and
delivery uses the broker's policy generation and trusted clock. Normal consumers
still reauthorize source and the dependency graph, even after a successful
prebuild future. A scope closes after its normal consumers have left; it waits
for its outstanding jobs, not unrelated principals' work. The global registry's
terminal cleanup detaches any still-owned scopes after joining its workers.

Focused evidence lives in `test_security_runtime_gtest` and
`test_input_script_cache_gtest`: diamond/parent imports, loose-file sibling
denial, symlink escapes, same-runtime and retained AST/MIR dependency denial,
exact-byte source refresh, case aliases, scope isolation, transport lifetime,
and scope-local teardown. `test_security_file_broker_gtest` additionally checks
that a selected data read does not authorize source identity acquisition.

## Input and parser resources

| Entry / owner | Bound-client acquisition | Limits / remaining surface |
|---|---|---|
| `lambda-eval.cpp::fn_input2`, including module initializers and ordinary file force | Explicit capsule client reaches `input_from_target_with_io`. FILE classification requires READ for a regular file or ENUMERATE for a directory before dispatch. Byte reads reacquire authority. | HTTP, system information and database services have no bound-client adapter yet and cannot enter their ambient dispatch. Legacy host calls retain their separate ambient path. |
| `input.cpp::input_from_local_path` | Data bytes use `security_io_read_bytes(..., source=false)` with the archive byte ceiling. Parser options carry the same borrowed client for this synchronous parse. | Inputs/caches do not retain a client pointing into a Runtime. |
| `input-dir.cpp` | Bound enumeration constructs the ordinary list of child Paths from broker results. No raw stat/list fallback follows a denial. | At most 4,096 visited entries and the broker byte ceiling; symlinks, hardlinked files, special files, protected objects and reserved stage names are filtered before delivery. Each child's optional metadata requires separate authorization. |
| `file_broker.cpp::security_file_input_kind`, `security_file_enumerate` | Policy precedes acquired-object classification/listing; held directory anchors and no-follow object checks enforce root/protected-object binding. Acquisitions are revalidated before returning the owned result. | Classification supplies only kind, not general metadata. Enumeration is bounded, nonrecursive, and fails without returning partial results on overflow or observed directory mutation. |
| `input-tex-latex.cpp` | Local/bundled probes and actual data reads use the client. Both `tex` and `latex` inherit the acquired document location. Inline `parse()` also carries its client's authority. | The parser's narrower beside-document rule and local file/byte budgets remain in force (D7.2.7v2). Bundled files require their own admitted read scope. |
| `input-tex-format.cpp::load_expl3_format` | Scoped engines build a private format with that client's bundled-resource reads. They cannot consume an ambient process-global format as authorization. | This preserves expl3 functionality at the cost of rebuilding scoped formats. A dependency-manifest cache is future optimization; denied scoped builds do not poison the host cache. |
| `input-graph-structurizr-resolver.cpp` | Scoped include paths normalize lexically, retain the narrower document-root check, then classify/read/list through the broker. | Existing depth/file/aggregate-byte budgets remain; network includes are still rejected. The byte limit applies during acquisition, replacing the old size-probe/read race. |
| `input-zip.cpp::fs_parse_uncached` | A captured member is parsed from immutable archive bytes with an explicit closed external-resource client. | Lazy member caches never retain live grants or external includes from an earlier caller. Archive-relative external-resource resolution is not implemented; ordinary byte-only formats continue to work. |

`InputParseOptions.io` and `EngineOptions.io` are synchronous borrowed inputs,
not persistent authority. A present client never becomes a NULL-client
ambient read after refusal. Ordinary pure parsing retains the same data model.

## Path metadata, traversal and task reads

Authority: **D7.5.2, D7.5.5**, reference operations **S2.4.4, S2.4.6–S2.4.7**,
context lifetime **D5.4.2–D5.4.4**, precise roots **D5.3.3**, total `len`
**S7.6**. Runtime acquisition now lives in `runtime/path_runtime.c` rather
than the representation implementation in `core/path.c`.

| Entry / owner | Bound-client acquisition | Limits / remaining surface |
|---|---|---|
| `path_runtime.c::path_load_metadata`, `lambda-proc.cpp::fn_exists` | METADATA is independently authorized; denied/absent results never trigger a raw stat fallback. Scoped getters reacquire rather than trust another context's captured `Path::meta`. | Total `exists` returns false on refusal. A successful broker stat can report authorized absence. |
| `path_is_directory`, `path_longest_existing_prefix` | READ/ENUMERATE classification uses the client before file/document navigation; these probes do not demand general METADATA. | Nonlocal external providers cannot enter an ambient scoped resolver. In-memory `temp` documents remain available. |
| `path_resolve_for_iteration`, wildcard traversal | One sorted directory facade supplies iteration and `*`/`**`, without raw stat/list fallback. Optional metadata is separately authorized. | Scoped closure limits are 4,096 entries across sibling subtrees and 64 levels; links are not traversed. Overflow/refusal discards partial output. Recursive result arrays have precise root frames. |
| `security_context.cpp` path snapshots | Context-owned capsule, semantic path keys and stable explicit GC roots. A broker session/namespace epoch invalidates snapshots on admission, restriction, revocation and observed expiry. Publication checks the acquisition's stamp again. | At most 4,096 entries; a stamp is not object authority. Actual acquisitions retain anchored object/protection checks. No scoped heap result is stored on a shared Path spine. |
| `lambda-eval.cpp::force_document` | READ/ENUMERATE is checked before reusing a captured document head. Scoped SYS provider forces and dynamic SYS members are denied. | Existing in-memory document versions are data; source cache hits continue their stronger exact-byte dependency reauthorization under D8.5.1v7. |
| `doc_context.cpp`, `core/path.c::path_get_root` | Document heads/node identities are realm capsules with owner-bound precise roots. Scheme roots allocate in the requesting pool, sharing the existing `path_new` constructor. | Another evaluator cannot read or clear these heads, or reuse a root pointer into a retired pool. Dynamic realm capsules release before heap destruction. |
| `concurrency.cpp::pn_io_read` | Owned path plus copied client enters the existing offload pool, with no Runtime/TLS access on the worker. Owner-stack delivery checks READ again before constructing a GC value. | Cancellation and scheduler teardown join the borrowed transport. Revocation before delivery suppresses the result. Unbound hosts retain their existing uv filesystem mechanism. |
| `concurrency.cpp::pn_io_exec3` | Scoped calls deny before constructing a process request. | This covers `io.exec`, not every process adapter. |

Root selection is additive across operation-eligible admissions. Relative
requests are expanded lexically against the default root, then all admitted
roots are considered. An entry-file scope cannot hide a separately selected
sibling; a narrow READ scope cannot hide an ancestor's explicit METADATA
scope. Shared policy preflight selects a namespace candidate without allocating
a resource handle or granting acquisition. Protected identities remain a veto.
The broker clamps its clock monotonically so rollback cannot restore expiry.

The initial profile now rejects unbrokered copy/move/delete/mkdir/touch/symlink/
chmod/rename, `cmd`, fetch, grep/text search, mail, secrets and loopback services
at their runtime adapters, before native acquisition or offload creation.
`io.exec` shares the same check. Dry-run fabrication cannot turn these refusals
into success. Database host authorization also refuses a bound caller before
driver open; this does not establish authority propagation for future offloaded
database adapters. These services remain unsupported until broker operations
and their qualifications exist (**D7.5.2, D7.5.5, D7.5.8**).

Write-set transactions, confinement and retained edit values now belong to an
evaluation-context capsule, with explicit-owner precise GC roots and teardown
(**D5.4.2–D5.4.4, D5.3.3**). Peer execution/reset/heap release cannot observe or
discard another owner's pending edits. A foreign document cannot enter the
current owner's set. In-memory document edits do not publish filesystem bytes;
`output()` remains the brokered publication route.

Remaining owners for **M1.3** include `main.cpp` admission, other process/network and account/device APIs, and
JS/Node/Jube loading. Named local-file authority comparison still consults
`shell_get_hostname` in `core/path.c`; immutable host-identity plumbing remains
part of the confined launch profile. Production CWD/root/resource mapping,
closed unsupported adapters, production worker/IPC admission, OS quotas and per-app separation
must be qualified before M2. This inventory does not treat an installed client
as a complete sandbox.

## Broker transport and experimental confined worker

**D7.5.2 / D7.1.1–D7.1.3** keep raw transport mechanisms in
`lib/ipc_stream.*`; `lambda/io/security_channel.*` owns the bounded wire codec
and callback dispatch. The mechanism adopts only an already connected local
stream, applies one monotonic deadline, rejects ancillary data, closes received
descriptors and returns no raw descriptor to the runtime. It does not establish
principal identity or grant permissions.

The trusted supervisor must own the private endpoint and authenticate/confine
its child. Session/sequence checks detect wrong connections and replay; a
session number or same-UID credentials alone do not authenticate a caller.
Seven acquisition callbacks are available. Grant/store/approval/native-loading
management is absent. Host callbacks retain their existing anchored-object
and policy checks. Request and response validation complete before the next
transaction; byte/request/deadline ceilings do not become a fallback path.
The broker reserves result space before invoking mutations. A lost write reply
carries an explicitly unknown outcome, and `output()` returns its own error
without retrying (**S7.4.5**).

`host/safety/worker.cpp` installs the channel client before entry acquisition
and joins its task/prebuild borrowers before transport teardown. The build
profile follows **D7.1.6**, excludes MIR/JIT and the DevTools/JS/DOM/Radiant/Jube
hosts, and contains no policy/file-broker/grant-store management implementation.
The signed macOS fixture now runs real entry/import/data/output, metadata,
recursive traversal and async reads over that channel. Its syscall control
proves only the observed read/write/create/TCP denials and inherited-fd case;
per-app separation, other egress/process/debug routes, concurrency with trusted
revocation/publication, quotas and production launch/persistence still need
M2 qualification. No complete offline sandbox release is claimed.

## Trusted admission and restoration

Under **D7.5.5–D7.5.7**, `host/safety/admission.*` now builds an ad-hoc
principal/source manifest before exposing its client, supplies a base protected
catalog and restores supported file selections from the matching store binding.
The additional host-only source capture obtains canonical name, bytes and
revision through `lib/file`'s same-descriptor capture. Changed/unlisted source
is rejected before delivery. No source parsing or initializer runs in the host.

`FileAnchorOptions.namespace_guard` checks the acquired parent and child name
before traversal, creation or enumeration. The broker uses it for absent and
existing protected namespaces, including Unicode/case aliases. Object guards
remain independent. Trusted catalog resolution does not grant authority from
a pathname. Shipped images remain mutation-protected through overlapping roots.

Supported remembered grants are reacquired and fingerprinted. The initial
profile restores no source/network/service capabilities. Store-generation leases
cover bounded broker operations and approval commits, with no lock held while
waiting for consent. IPC-delivery/revocation races, persistent OS bookmark and
inode-reuse qualification, complete installed-app/global catalog coordination,
worker resource-path mapping, production UI and process supervision remain open.
The signed fixture demonstrates actual restart/grant/revocation behavior but is
not the production launch path; ordinary CLI execution remains unbound.
