# App-safety qualification

Authority: **D7.5.1–D7.5.2, D7.5.5–D7.5.8** in
[formal design](../../doc/Lambda_Formal_Design.md). Delivery/status:
[implementation plan](../../vibe/impl/Lambda_Impl_App_Safety.md).

The policy evaluator is a neutral, broker-side component. Its resource IDs
refer to objects the broker has already acquired and classified. It does not
authenticate callers, resolve filesystem aliases, issue consent, or contain
native code. Host-only management entrypoints must never be exposed directly
to a worker. Adapters must preserve the dispatch generation when checking an
async result; obtaining a fresh generation at completion loses that check.
Refresh the trusted clock on every check, including completion, so expiry is
not evaluated using the earlier dispatch timestamp.
Grant lifetimes support session ownership and explicit expiry. The host store
serializes persistent records; the file broker implements single-write approval
challenges. Ad-hoc admission/restoration and a macOS supervisor with once-only terminal
consent are implemented; production default integration and persistent
permission UI remain pending.

`test_security_policy_gtest` covers project/loose-file defaults, source
admission, protected objects, ceilings, selected-object grants, default network
denial, grant expiry, revocation, cross-session handles and bounded tables.
`test_file_module_gtest` includes atomic no-replace publication: binary/empty
outputs, existing files, directory/symlink rejection, and simultaneous
publishers with exactly one intact winner. The pathname-based publication
primitive requires a trusted parent directory; the rooted file broker is
implemented as a separate host component. It still requires authenticated IPC
and actual worker confinement before exposure to untrusted code.

`test_security_file_broker_gtest` exercises real rooted reads/source requests,
exclusive create, private storage, target/version/payload-bound overwrite and
append, deadlines, revocation, budgets, aliases and protected-directory
substitution. `test_security_runtime_gtest` executes Lambda `output()` through
that client on the interpreter and MIR JIT, including ZIP and denial before
parent creation. It also covers real file-backed source/import/cache loading,
revocation, exact-byte refresh and canonical case aliases. Scoped prebuild
ownership and teardown are covered by `test_input_script_cache_gtest`.
The runtime suite also covers independent metadata authority, document probes,
bounded recursive walks, context-owned snapshots, revocation before cached
force/task delivery, active-read cancellation, in-memory documents and denial
before `io.exec` creates a process. These run in both execution tiers, with
focused forced-GC/poison and cache-disabled qualification recorded in the plan.
See the [loading inventory](Loading_IO_Inventory.md) and
[host component contracts](../../lambda/host/safety/README.md).
It is a bound-client integration test; ordinary CLI launches remain unconfined.

After the build configuration has been generated:

```sh
make -C build/premake config=debug_native test_security_policy_gtest test_security_store_gtest test_security_file_broker_gtest test_security_runtime_gtest test_file_module_gtest -j4
./test/test_security_policy_gtest.exe
./test/test_security_store_gtest.exe
./test/test_security_file_broker_gtest.exe
./test/test_security_runtime_gtest.exe
./test/test_file_module_gtest.exe
```

For aggregate library correctness, use the shared runner's library baseline
selection with `--exclude-test=test_avl_tree_perf`. That runner measures
performance and requires a release build under AGENTS.md rule 10; it is not
part of this debug correctness gate.

## macOS platform probe

```sh
python3 test/safety/test_macos_sandbox.py
```

The runner compiles two copies of the same raw-syscall C probe, stages app
bundles under `./temp/app_safety_macos_probe/`, and signs a control and an
App Sandbox worker. The default identity is ad hoc; `--sign-identity` can name
a local identity. No private sandbox API is used. It compares opening test
sentinels for reading/writing, creating a sibling, and connecting to a local
fixture listener. Production grants and real user settings are not touched.

The control must succeed and the worker must return `EPERM`/`EACCES` for every
operation. If the control fails under an outer development sandbox, the result
is inconclusive and the command exits nonzero. The runner needs loopback socket
permission and permission to execute the locally signed helper. Build/signing,
launch or timeout failures also exit nonzero, with evidence in `result.json`.
Artifacts are retained for diagnosis.

A passing result means **basic syscall denials only**. M2 remains pending:
per-principal containers/IPC, process and debug access, inherited handles,
JIT/interpreter profiles, quotas, policy-store protection, approval UI and the
actual Lambda worker must pass their own gates. In particular, one common
helper's sandbox/container is not evidence of separation between Lambda apps.

Observed on 2026-11-02: macOS 26.5.2 arm64, ad-hoc signed hardened-runtime
probe; all four control operations succeeded and all four worker operations
returned `EPERM`. The first run under the development sandbox was inconclusive
because its fixture listener could not bind; the authorized local-socket run
passed. This is a platform feasibility result, not a shipped sandbox profile.

## Anchored files and the host grant store

`FileAnchor` in `lib/file.*` holds a directory descriptor and revalidates its
path binding before use. It walks each component without following symlinks,
rejects regular-file hard links and special files, and can require trusted
parents, private permissions and one device. Guard callbacks retain paths
relative to the original root even when a child anchor is used. Reads and
atomic writes recheck their guard before delivery/publication. The broker must
bind those guards to the original request generation and refresh the trusted
clock; a raw anchor is not an authorization service.

No-replace publication now uses macOS `RENAME_EXCL` or Linux
`RENAME_NOREPLACE`, with no clobbering fallback. This avoids a crash leaving
both a canonical record and its staging name as hard links, which the strict
reader would correctly reject. Actual filesystem support is required.
Windows anchored operations currently return `ENOTSUP`; the earlier Windows
pathname-based exclusive publication remains available. A device check alone
is not Linux mount/namespace confinement, and metadata revision checks are
not universal atomic compare-and-replace against external writers.

[grant_store](../../lambda/host/safety/grant_store.h) is a host-only component
under **D7.5.7**. Its source is linked by the dedicated supervisor and store/
admission test executables; it is excluded from ordinary runtime/worker source
lists. Default CLI integration and installed publisher qualification remain
pending.

The production constructor resolves the OS account's home with `getpwuid_r`,
then acquires `.lambda/setting` through private directory anchors. It ignores
`HOME`, `LAMBDA_HOME` and working-directory path overrides. The fixture-home
constructor exists only with `LAMBDA_SECURITY_STORE_TESTING`; no production
environment variable or CLI setting redirects the store. Tests never invoke
the constructor that creates the real account's settings directory.

V1 records have an explicit little-endian encoding, independent of C struct
layout: a 128-byte header, bounded grant rows (64 fixed bytes plus a locator),
and a 32-byte SHA-256 checksum. The header binds principal kind, host-assigned
identity, admitted code/root digests, generation and count. Reserved fields,
unknown versions/operations/kinds/flags, duplicate resources, malformed lengths,
embedded NULs, truncation, extra data and checksum damage are rejected. Limits
are 256 grants, 4,096 locator bytes and 1 MiB per record. The checksum detects
damage; the OS/broker boundary must establish authority to mutate the store.

Only explicitly persistent grants can be saved. Expiry is retained as UTC
epoch seconds (zero means no expiry); the service restoring a grant must check
it against trusted time and current resource/profile policy. Serialized
locators and fingerprints do not themselves grant resource access. A changed
code/root binding returns no grants. Trusted re-admission clears all old
grants before the new binding can be used.
Principal admission and transitive source/version tracking remain unimplemented;
the store's equality check is not itself proof that code or resource acquisition
has been admitted.

A private, identity-checked lock file serializes independent store instances.
Updates compare the current generation before atomically publishing its
successor. Revocation writes an empty tombstone rather than deleting the
record. There is no automatic recovery from a backup or staging file. A stale
writer cannot restore an older grant set. If publication commits but directory
sync fails, that store instance becomes unavailable instead of acknowledging
durable permissions.

The store tests cover reopen, stale writers, identity changes, cross-principal
replay, malformed records with recomputed checksums, path/lock substitution,
links, unsafe permissions, simultaneous writers and acknowledged revocation
after a writer is killed. These are persistence-component checks. Same-user
malicious native code is contained only after M2's actual OS worker/broker
integration is implemented and qualified.

## Real Lambda worker and broker IPC

```sh
make build-safety-worker
python3 test/safety/test_macos_worker.py
```

The worker target is a T0-only runtime bootstrap under **D7.1.6 / D7.5.5**.
The test builds a separate native-probe variant and the host's channel fixture,
then signs both worker entrypoints in App Sandbox bundles. Neither worker
links the grant store, file-broker management or policy-management code. The
trusted Python launcher sanitizes the environment and inherits only explicit
stdio and the private channel. Session numbers identify the admitted channel;
they are not authentication in place of private endpoint ownership.

Seventeen cases cover an unsandboxed control, a confined create, denied and
host-approved replacement, outside/protected input refusal, a path/task case
and the plain bootstrap executable, plus eleven unsupported-adapter refusals
inside the confined worker. They execute actual Lambda imports,
JSON data reads, output, recursive traversal, metadata and async reads. Native
read/write/create/connect calls run beside that workflow, bypassing Lambda.
The control must succeed; the signed probe must deny all four and show zero
unexpected inherited descriptors. The fixture tests never open real settings.
Its automatic approval is a trusted **test fixture**, not production consent UI.

Eight cases use the host-only ad-hoc admission core (**D7.5.5–D7.5.7**) rather
than directly constructing a file broker. They use real fixture settings beneath
the selected ancestor, test reads/writes against those protected settings, and
receive the session only after source admission completes. Separate trusted host
processes remember a file grant, restore it after restart, persist revocation and
verify its absence after another restart. Changed transitive source stops launch
before the worker starts. No real account settings are opened. The fixture's
store-home constructor is compiled only with `LAMBDA_SECURITY_STORE_TESTING`.

`test_security_admission_gtest` covers source/root/profile binding, source refresh,
new-file/default replacement behavior, persistent versus session/expired grants,
same-path resource substitution, independent scripts, protected/unsupported
selections, managed-code overlap, source bounds and shipped-code acquisition.
The store suite also checks generation leases against concurrent revocation;
the broker suite checks absent-name aliases and held-source capture. Signature
checks verify the real management symbol prefixes, including
`security_grant_store_*` and `security_admission_*`, are absent from both workers.

Artifacts and exact executable/dependency/signature metadata remain under
`temp/app_safety_macos_worker/`. `--no-build` reuses binaries already built
from the current source; use the default for a complete build/run. Signing,
launch, control or expected-output failures return nonzero. The original
`test_macos_sandbox.py` retains its standalone native-syscall control and
shares the bundle-signing mechanism.

`test_security_channel_gtest` is in the library baseline. It covers all seven
callbacks, concurrent transactions, protected/revoked resources, unknown
management opcodes, replay, malformed envelopes/replies, truncated/stalled
streams, payload/request/byte budgets, missing channels, descriptor rejection
and lost write replies. `test_security_runtime_gtest` checks that an unknown
write outcome is returned as an owned error without an automatic retry for
text, binary, JSON and ZIP in both tiers, including forced-GC qualification
(**S7.4.5, D5.3.3**).

This is stronger M0/M2 component evidence than the earlier syscall-only probe,
but `m2_complete` stays false. The separate supervisor runner below now covers
verified child launch, once-only terminal consent and resource monitoring.
Installed principal admission, complete grant/UI integration, per-app separation,
process/debug access, complete watchdogs/quotas,
publication/revocation races and closed unsupported adapters still require
qualification. The acquisition/format/parser services and protocol details are
listed in the [inventory](Loading_IO_Inventory.md) and
[component contract](../../lambda/host/safety/README.md).


## Verified macOS supervisor

```sh
python3 test/safety/test_macos_supervisor.py
# Reuse already-built host, fixture, worker and native quota probe:
python3 test/safety/test_macos_supervisor.py --no-build
```

This runner calls the actual `security_supervisor_run`, not Python's worker
launcher. It stages signed bundles below `./temp/app_safety_macos_supervisor/`
and runs real Lambda imports, input and create/overwrite output. The host
verifies the static bundle and suspended child's identity before resume. Cases
reject missing/extra entitlements, wrong identifiers/digests and tampered code
or resources before launch. A native probe checks inherited handles, environment,
new session, terminal denial, raw project reads, egress and process creation.
Separate probes force wall/CPU/memory/output limits and malformed IPC.

A pseudo-terminal belongs only to the trusted host. Tests type yes/no after
reading its concrete prompt, change the target while consent is pending and
submit consent after the watchdog stops the worker. They also check that
worker output is not delivered while approval is pending. The runner drains
terminal echo during teardown, as required for a Darwin controlling terminal.
Headless consent defaults to denial. No test supplies approval through a worker
frame or production grant store.

The fixture account home, exact ad-hoc image pin and quota overrides are
compiled only into `lambda-safety-host-fixture.exe`. The production host requires
an Apple-issued, hardened-runtime publisher signature with no entitlements and
a fixed companion worker signed by that publisher. Unsigned checkout binaries
must refuse production launch. The runner records source-independent binary
hashes, concrete outcomes and OS version in `result.json`; it does not qualify
an installed certificate-backed distribution.

Physical-footprint and CPU gates are sampled, not zero-overshoot kernel caps.
macOS supplies a sandbox-container HOME after exec. A helper identifier still
shares that OS container, so per-principal private storage, complete catalogs,
persistent UI/root bookmarks and remaining M2 gates
stay open. Passing this runner does not enable the default CLI sandbox.


## Host package-update controls

```sh
python3 test/safety/test_update_controls.py
# Reuse the built host/fixture and focused native test executable:
python3 test/safety/test_update_controls.py --no-build
./test/test_security_update_gtest.exe
```

The native suite checks protected-state parsing/profile identity, independent
host reservation and publication/disable ordering, preference retention,
cadence/backoff/deadlines, manual/offline precedence, malformed endpoints and
unchanged app file grants (**D7.5.7–D7.5.8**). The console runner uses an isolated
fixture home and separate host processes for every command; it tests package
versus `--all` scope, restart persistence, pause/disable/restore, offline,
malformed arguments, corrupt state and production signature/fixture-option
refusal. A test-only native seed mode supplies public fixture package bindings;
production console admission of arbitrary endpoints is intentionally absent.

All artifacts stay below `./temp/app_safety_update_controls/`; no real account
settings are opened. `delivery_available: false` is a required honest status:
these tests qualify controls, not HTTPS fetching, TUF authenticity/freshness,
extraction, installation, activation or request-trace privacy. The trusted
installer/verifier must establish public configuration/authenticity before using
the admission API. Network denial for apps and the existing seven-operation
worker protocol remain unchanged.


## Response-publication revocation

`./test/test_security_admission_gtest.exe --gtest_filter='SecurityDeliveryTest.*'`
runs eight native integration cases under **D7.5.2 / D7.5.5–D7.5.7**. They exercise
the real admission/store and guarded LSAF dispatcher: all six reply shapes after
local revocation, saved-generation revocation, expiry before/during publication,
confirmed writes, bad capture, malformed requests and a backpressured peer with
a concurrent revoker. The 250 ms publication deadline is independent of the
60-second request deadline. These cases establish kernel publication ordering;
they do not claim to erase bytes already queued or received. The signed
supervisor runner also exercises guarded dispatch and long human consent waits.

`async_fixture.h`, `grant_fixture.h` and `channel_wire_fixture.h` share joined
worker lifetimes, saved-grant revocation and fixed wire-header construction
across the native component tests. All artifacts remain under `temp/`.
