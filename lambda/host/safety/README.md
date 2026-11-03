# Host and worker safety components

The broker-side components implement **D7.5.2, D7.5.5–D7.5.8**.
`grant_store.cpp`, `file_broker.cpp`, `admission.cpp` and `supervisor.cpp`
now belong to the dedicated host executable and isolated qualification fixtures;
they are absent from worker images. The same host now includes protected package
update controls and console management; delivery is not yet implemented.
Host ad-hoc admission binds source identity,
populates a base catalog and restores supported grants. An experimental macOS
supervisor verifies and launches the offline T0 worker, handles once-only terminal
consent, and monitors resources. Default CLI integration, certificate-backed
release packaging and the complete installed-app/private-directory catalog
remain pending.

`worker.cpp` is the experimental runtime bootstrap for a fresh, already
confined process. It installs its IPC client before canonicalizing, reading,
parsing or initializing the entry. It does not install OS containment or
admit grants. The dedicated supervisor now verifies launch and binds grants;
installed publisher qualification and default CLI integration remain pending.
Ordinary CLI execution retains its existing unbound model.

`file_broker.h` owns a policy, anchored roots, a protected-directory catalog,
bounded request payloads and overwrite challenges. Host admission installs the
complete catalog before roots. Directory identities are checked on absolute
ancestors as well as relative traversal, including case aliases. Catalog or
root replacement fails closed. Selected file admissions pin one leaf and its
revision; they grant no siblings. Private storage additionally requires private
owner permissions and trusted parents. Links, special files, cross-device
traversal and reserved atomic-staging names are rejected.
Reserved-name classification uses compatibility case folding and removes
ignorable Unicode characters, covering observed APFS aliases such as long-s.
This deliberately reserves a superset of spellings; it never resolves or
rewrites the requested filesystem path. Invalid UTF-8 fails closed.

The client in `lambda/io/security_io.h` has read/source-read, source identity,
write, metadata, kind, directory and snapshot-stamp operations. It exposes no
approval, grant, store, descriptor or anchor operation. Relative names expand
lexically from the default root; all operation-eligible admitted roots are then
considered, including separately selected siblings. Host-only policy preflight
selects a candidate without issuing authority; actual acquisition still checks
its lease and anchored object. Dot components, empty
components, backslashes and paths deeper than 64 components are rejected.
Root spelling mismatches can deny access; no lexical match establishes authority.

New files publish with native exclusive rename. Existing replacement, in-place
write and append require their own operation. A project overwrite challenge
retains a private payload copy, digest, exact target revision, principal/session,
root, policy generation and deadline. The host obtains its display information
from `describe`, then calls `approve` or `cancel`; it holds no broker lock while
the user decides. Approval is single-use. Reopening requires new authority.
Permission changes conservatively invalidate other pending challenges. Expired
or invalidated requests are reclaimed on subsequent writes; retained memory is
bounded even without another request. Byte and mutation-attempt budgets cover
empty files and failed/partial writes. They do not replace OS resource limits.

`SecurityIOResult` distinguishes denial, an approval challenge, and mechanism
errors. `committed` means publication or in-place mutation occurred; an error
with `committed == true` can leave partial or non-durable bytes and must not be
retried blindly. Atomic-save checks detect observed version/namespace changes;
metadata checks are not atomic compare-and-swap against arbitrary external
writers. The eventual platform transaction still needs M2 race qualification.

The runtime client binding follows **D5.4.2–D5.4.4** through a context-lifetime
capsule extension, without changing `EvalContext` layout. The host must outlive
its client binding. `output()` consults it for raw, binary, structured, lazy-file
and ZIP output, and delegates parent creation to the broker. Current CLI launches
do not install this binding. Other runtime I/O, non-Lambda loaders and production
admission remain pending, so neither the binding nor these component tests establishes
a confined execution profile. A hostile native worker requires the actual OS
boundary and authenticated IPC before these management APIs are meaningful.

Lambda entry/import loading now obtains canonical source names from a
source-authorized held descriptor, then reacquires exact bytes through the
client before cache lookup. Cached dependencies are checked before Runtime
hits or AST/MIR graph cloning, including private/cache-off images. Context-owned
prebuild scopes isolate authorization and transport lifetime while retaining the
shared bounded pool. Their scopes close after normal consumers have left and
join only their own jobs. Source clients never take the ambient JS fallback in
the initial Lambda-only profile. The callsite and remaining-route inventory is
[`Loading_IO_Inventory.md`](../../../test/safety/Loading_IO_Inventory.md).

Ordinary `input()` and inline `parse()` now carry the same client to data,
directory, TeX and Structurizr acquisition. Directory results have bounded
owned names/kinds and separately authorized metadata; the raw anchor visitor
revalidates before delivery. Scoped expl3 formats build privately until a
reauthorizable format-cache dependency manifest exists. Captured archive
members cannot acquire external resources through their lazy parse cache.
Path metadata, `exists`, directory traversal, wildcard force and async
`io.read` also use the client (**D7.5.2, D7.5.5**). Directory walks share a
finite entry/depth budget. Scoped snapshots belong to their EvalContext and
have precise heap roots; broker session/namespace epochs invalidate them on
authority changes and expiry. A stamp cannot authorize object acquisition.
Task reads carry owned job data and recheck authority before delivery;
cancellation joins active work before transport teardown. Scoped `io.exec`
is denied before process creation. Other unsupported legacy mutations/search,
command, fetch, mail, secret and loopback adapters now refuse bound callers
before native work, including dry-run fabrication. Transactions and retained
edits have context-owned capsules and precise roots. Full service inventory
closure, immutable host identity and production admission remain pending.

Focused checks:

```sh
./test/test_security_policy_gtest.exe
./test/test_security_store_gtest.exe
./test/test_security_file_broker_gtest.exe
./test/test_security_admission_gtest.exe
./test/test_security_runtime_gtest.exe
```

The policy/store/broker/admission suites are library tests. The runtime suite executes actual Lambda
procedures in the interpreter and MIR JIT. Build definitions live in
`build_lambda_config.json`; use `make` to regenerate them. The implementation
ledger and aggregate evidence are in
[`vibe/impl/Lambda_Impl_App_Safety.md`](../../../vibe/impl/Lambda_Impl_App_Safety.md).

## Private broker channel and T0 worker

[`security_channel`](../../io/security_channel.h) serializes the seven
acquisition callbacks over a privately inherited, connected AF_UNIX stream.
Raw descriptor transfer stays in `lib/ipc_stream.*`; the neutral I/O layer
owns framing and dispatch, preserving **D7.5.2 / D7.1.1–D7.1.3**.
The trusted launcher owns endpoint creation, child identity, confinement,
environment and handle inheritance. A claimed principal, a session number
or same-UID peer credentials cannot establish admission. The connection's
nonzero session must match its admitted broker session; each connection has a
new identity and sequences start at one. There are no reconnects or retries.

Version 1 has a fixed 32-byte little-endian header:

| Offset | Field |
|---|---|
| 0 | Four bytes `LSAF` |
| 4 | Version `u16 = 1` |
| 6 | Operation `u16`, 1–7 |
| 8 | Strictly next sequence `u64` |
| 16 | Host-assigned connection/session identity `u64` |
| 24 | Bounded payload length `u32` |
| 28 | Flags `u32`: zero for requests, one for responses |

Each response starts with 32 bytes: decision, nonnegative system error,
commit/unknown-outcome flags, a zero reserved word, then approval session and
serial. Each first group field is `u32`; approval fields are `u64`. A path or
name is a `u32` byte length followed by non-NUL bytes, bounded below 4,096
bytes. No `Item`, pointer, native structure layout or descriptor crosses IPC.

| Opcode | Request after path (STAMP has no path) | Successful response after result |
|---|---|---|
| 1 READ | `u64` byte ceiling, `u32` source/data flag | Owned bytes; the frame supplies their length |
| 2 WRITE | `u32` append/atomic flags, remaining payload bytes | No additional data |
| 3 SOURCE | Empty | Source-authorized canonical path |
| 4 KIND | Empty | `u32` FILE or DIRECTORY |
| 5 ENUMERATE | `u32` entry ceiling | Count, names/kinds, optional independently authorized metadata |
| 6 STAT | Empty | Explicit 32-byte metadata encoding |
| 7 STAMP | Empty | `u64` session and authority epoch; not an acquisition capability |

Metadata encodes size/modified/created as 64-bit words, then mode and kind/
existence flags as 32-bit words. Directory entries encode name, directory/
metadata flags, then optional metadata. The broker retains all actual path,
alias, protected-object, grant and lease checks.

Unknown versions, opcodes and flags, wrong sessions/sequences, replay,
truncation, trailing method data and malformed replies close the channel.
Ancillary data is rejected, and any received descriptors are closed. Missing
or failed transports never select ambient I/O. Client calls serialize the
whole transaction, including reply validation. Fixed monotonic deadlines
cannot be renewed by a slow peer. The server also applies this bound while
awaiting the next frame, so it is an idle bound for this experimental profile.
Payload/request/cumulative-byte limits are finite; the host reserves a result
before invoking mutations. EOF is clean only between complete requests.

A lost reply after a complete write request has an explicitly unknown outcome.
`SecurityIOResult.outcome_unknown` is distinct from `committed`; neither a
confirmed commit with an error nor an unknown outcome permits a blind retry.
`output()` carries that failure as an owned error for text, binary, structured
and ZIP output (**S7.4.5**). No grant, approval, store, endpoint creation or
native loading operation exists in this schema.

Build the bootstrap with `make build-safety-worker`. It extends the reduced
host under **D7.1.6**, excludes MIR/JIT, networking, DevTools, JS/TS, DOM,
Radiant and Jube host modules, and explicitly excludes policy management.
It retains required image/font mechanisms and their libraries. Its current
limits are 1 MiB frames, 64 MiB cumulative channel bytes, 8,192 requests and
60-second transaction/idle bounds, allowing a host consent wait; the supervisor
independently caps the whole process at 30 seconds. It has no JIT entitlement. Runtime teardown
joins task/prebuild borrowers before destroying the transport.

The [signed-worker fixture](../../../test/safety/test_macos_worker.py) runs
actual Lambda entry/import/data/output/path/task behavior in App Sandbox and
compares native syscall attempts with an unsandboxed control. Consent there
is a trusted test-parent choice. The separate supervisor runner below qualifies
once-only terminal consent and bounded monitoring. Per-app container separation,
complete process/debug containment, publication/revocation races, complete quotas,
persistent permission UI, unsupported-adapter closure and installed publisher
qualification remain M2 gates.

## Host ad-hoc admission and remembered selections

[`admission.h`](admission.h) implements the host side of **D7.5.5–D7.5.7**.
It receives a trusted entry/project selection, installation paths and additional
protected-directory declarations. It never parses or executes script source.
Before publishing its client, it captures code through the file broker, binds
the principal to the acquired entry and account/profile store, constructs a
source/root fingerprint, and loads only that complete binding's records.
Fresh active principal/session numbers come from OS-backed cryptographic entropy;
the privately inherited connection remains the actual channel capability.

The exact entry and root-local `.ls` files form a per-launch manifest. Limits
are 1 MiB per source, 1,024 unique sources, 32 MiB of unique source bytes,
4,096 enumerated entries and 64 levels. Canonical name, bytes and revision come
from one held file; source dispatch rechecks revision and digest before delivery.
Changed or newly introduced code cannot execute with the earlier admission's
grants. A fresh changed-code/root admission requires explicit host revalidation,
which clears old grants. Ordinary data/output changes do not change the root
object's identity. Approved shipped code is read-only even through an overlapping
project/selected-parent grant; temporary host enumeration rights are revoked
before the worker receives the client. Worker package-path mapping remains
production-launcher work.

The catalog protects the actual anchored settings directory, mutations of the
host `.lambda` area and installation/shipped images, base OS directories, account credentials/configuration and
host-supplied private app directories. Named exclusions pin an acquired parent
and conservatively folded child name, so absent sensitive names cannot be
created later through a broad ancestor grant. Lookups, root admission and
enumeration apply that check before acquiring a child. Existing objects also
retain identity checks. The broker bounds the catalog to 128 entries. This is a
base provider, not proof of a complete cross-app/platform catalog; the installer
and production coordinator must supply and refresh their remaining declarations.

Trusted selectors can add session/persistent existing-file or recursive-directory
grants for supported file operations. Restoration reacquires each resource and
compares its fingerprint; expired, changed, protected or unsupported records do
not restore authority. Source/network/service grants are not restored by this
offline profile. Persistent changes use the store's generation CAS. Each bounded
I/O/approval operation holds a store-generation lease, serializing acknowledged
host saves with prior acquisitions/commits; consent waits hold no such lease.
An unexpected store generation closes the live admission. Revocation before
final IPC delivery, crash/publication races, inode reuse and persistent native
resource/bookmark identity still need M2 qualification. Filesystem fingerprints
are not claimed to be universal persistent OS capabilities.

Inspection and revalidation are host management APIs and cannot be called over
the worker protocol. Inspection returns remembered records, not proof of their
current effective authority. Close an admission only after all borrowed clients
and channel dispatchers have joined. Corrupt/unavailable stores currently stop
this admission path; trusted repair/default-recovery UI remains to be built.
Ordinary CLI launches still do not install this service.


## Experimental macOS supervisor

`make build-safety-worker build-safety-host` builds `lambda-safety-worker.exe`
and `lambda-safety-host.exe`. These are separate executables; the normal
`lambda.exe` still launches without this profile. The production host expects
`LambdaSafetyWorker.app` beside its own executable, with identifier
`org.lambda-script.safety-worker`. A release installer must package/sign that
bundle and the host with the same Apple-issued publisher identity and hardened
runtime. The host has no entitlements; this worker profile admits exactly
`com.apple.security.app-sandbox = true`. An unsigned/ad-hoc host refuses launch.
No signing key is included or generated by the runtime.

The operator invokes the host with an absolute selected entry and optional
`--project ABS_DIR`. Omitting the project selects the entry leaf only. Explicit
`--revalidate` clears obsolete code/root grants through the existing admission
contract. `--ask-overwrite` enables a bounded `/dev/tty` prompt; otherwise an
existing-file mutation is denied. The current UI offers **once** only. It shows
the broker-held target, operation, payload length/digest and target identity;
control bytes in paths are escaped. Worker output is buffered until consent and
teardown finish. The host obtains no approval from stdin, a worker frame, a
source manifest or an environment variable. Session/persistent selection and
revocation UI are still pending; the underlying host management APIs exist.

`lib/confined_process.*` owns platform mechanisms (**D7.5.2**): strict sealed
bundle verification; Apple publisher and exact worker identity/entitlement
checks; suspended `posix_spawn`; dynamic child code-hash comparison; new process
session; clean environment; explicit fd 0..3; output pipes; cancellation and
process reaping. The OS receives only `PATH` and `LANG` from the launcher.
macOS subsequently supplies its own sandbox-container `HOME`; this is recorded
by the native probe and is not a parent environment leak. The broker endpoint is
privately inherited and has no discoverable listening socket. The dispatcher is
constructed before resume and joined before admission/store destruction.

The monitor polls at up to 20 ms intervals. Defaults are 30 seconds elapsed,
10 seconds CPU, 512 MiB physical footprint and 1 MiB combined stdout/stderr.
CPU sampling converts Mach ticks with the platform timebase. Output is capped
in the host; memory/CPU sampling is not a kernel reservation or a zero-overshoot
ceiling. Trusted worker bootstrap also installs hard `RLIMIT_CPU` (10 s),
`RLIMIT_FSIZE` (1 MiB), `RLIMIT_NOFILE` (64), `RLIMIT_NPROC` (0) and `RLIMIT_CORE`
(0) before runtime initialization. On quota/protocol failure the host kills the
private process group, discards subsequent consent and tears down IPC. Other
platforms return unsupported, with no unconfined fallback.

`test/safety/test_macos_supervisor.py` uses a separately compiled **test-only**
host with a fixture account home, pinned ad-hoc worker and tighter test budgets.
Those options are absent from the production executable. It never opens the
real `~/.lambda/setting`. Apple container services may create the ordinary test
helper container, as in the existing macOS probes.

This does not complete **M2 / D7.5.1, D7.5.5–D7.5.7**. In particular, one helper
identifier still has a shared OS container; per-principal container/private
storage isolation, complete catalog/installed-app admission, persistent root
bookmarks, shipped-resource mapping, full
race/crash/debug/mapping coverage, kernel memory/thread/storage policy and
certificate-backed deployed qualification remain required. No default sandbox
or package-update service is shipped by this component.

Platform references: [Apple static code validation](https://developer.apple.com/documentation/security/secstaticcodecheckvalidity(_:_:_:)),
[validation flags](https://developer.apple.com/documentation/security/static-code-validation-flags),
[spawn attributes](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/posix_spawnattr_setflags.3.html).



## Authorization through response publication

Under **D7.5.2 / D7.5.5–D7.5.7**, the supervisor uses
`security_admission_delivery_guard` with guarded channel dispatch. After complete
request validation, the host captures its authority stamp before invoking the
operation. Before publishing the response it checks that same session/epoch,
then holds the admission mutex and protected-store generation lease through the
bounded send. A fresh generation sampled only after the operation would miss
local revocation of already-buffered bytes.

Session revocation, saved-grant changes and observed expiry suppress buffered
read, source-path, kind, directory, metadata and authority-stamp replies. Denial
clears approval references and retains any confirmed commit or unknown-outcome
status (**S7.4.4–S7.4.5**). A committed write must not be retried automatically.
Invalid capture prevents dispatch, including mutation callbacks. No publication
lease crosses request receipt, operation callbacks or consent; each operation
continues to use its own admission lease.

The production publication budget is 250 ms, separate from the request/consent
deadline. Guarded stream transfers check expiry before/after polls, cap each poll
at 25 ms and each send at 64 KiB, and release the lease on every exit path. A
blocked peer therefore cannot retain the permission lease for the 60-second
request budget. Scheduling and lock-acquisition latency are not hard real-time
bounds. Revocation acknowledgment follows preceding publication leases; bytes
already queued in the kernel or delivered to the peer cannot be recalled.

The worker API and LSAF v1 wire schema are unchanged. Unguarded neutral channel
entrypoints remain available for local/test adapters; admitted production
supervision and the admission fixture explicitly select guarded dispatch.
Eight integration tests in `test_security_admission_gtest` cover buffered-reply
revocation, expiry, mutation outcomes, bad capture, malformed input and an unread
peer with concurrent saved-grant revocation. This closes the selected IPC delivery
gap, while the other M2 release requirements above remain open.


## Protected package-update controls

[`update_controls.h`](update_controls.h) implements the **D7.5.7–D7.5.8**
control plane. It neither fetches nor verifies remote metadata. A trusted
installer/verifier must supply authenticated public package configuration; a
root fingerprint in this API is a binding, not proof of a valid remote signature.
The source, trust roots, signed metadata client, transport, staging and activation
remain the next M3u work. No update command or request-now handle exists in worker
IPC, and app networking remains denied by its existing policy.

The host catalog is `~/.lambda/setting/package-updates.state`. It uses the same
acquired account directory, ownership/link checks, mutex, cross-process lock and
atomic revision-checked publication as grants. `state_store.h` offers a fixed
host-only record slot, not arbitrary filenames or a second worker I/O API.
A checksum detects inconsistent bytes; OS confinement must protect the store.
The v1 catalog binds the canonical profile directory and device/inode identity,
so copying it into another profile does not admit its packages. Missing state
requires explicit create-only initialization. Corrupt/unknown state never falls
back to enabled defaults, a backup, or environment-selected settings.

Each of at most 128 packages records a public identity, canonical HTTPS metadata
base, up to eight exact HTTPS origins, channel, publisher/root fingerprints,
host interval/job/download ceilings and user choices. The selected profile
rejects credentials, query/fragment/percent aliases, dot segments, noncanonical
host spellings, IP literals and known local hostnames. DNS/public-address/redirect
validation still belongs to the pending transport. Public-path semantics and
publisher authenticity come from trusted admission, not string validation.

Global and per-package enabled/paused/disabled states intersect. A pause may
have a UTC expiry or remain indefinite. Restore enables only that selected
scope, retains configuration and other scopes, and never changes file grants.
Offline overrides every schedule and explicit manual check. A trusted manual
check can bypass pause/disable for one job without enabling schedules; apps have
no caller path to that operation.

Jobs use persisted per-package expiring random reservations, bounded cadence
and capped exponential failure backoff. Store generation changes for scheduling;
a separate policy epoch changes for control/identity decisions. Scheduling one
package therefore does not revoke other active jobs. Pause/disable/offline/
revalidation invalidates all older tickets, including tickets in other host
instances. Live controllers retain observed generation/epoch maxima, and an
observed-invalid or expired ticket cannot revive after clock rollback or restore.
This is not restart-proof anti-rollback storage; TUF trust/version persistence
and protected recovery remain unimplemented.

`security_update_job_enter` revalidates a ticket and holds one bounded local
publication lease. A control change in another host cannot acknowledge until
that earlier publication releases its lease. Release on the acquiring thread;
never wait for network, extraction, UI, or call another control API while held.
Future transports/installers must check at dispatch, completion and activation
and cancel on refusal. Reserving a job alone does not grant a fetch or authorize
an install. Closing a controller cancels its tickets; references keep the closed
owner alive until tickets are freed, without later touching its destroyed store.

After publisher-qualified installation, the trusted console supports:

```sh
lambda-safety-host.exe updates init
lambda-safety-host.exe updates status
lambda-safety-host.exe updates pause lambda.doc
lambda-safety-host.exe updates disable --all
lambda-safety-host.exe updates restore lambda.doc
lambda-safety-host.exe updates offline on
```

`--all` is explicit; a package named `all` remains an ordinary package. Optional
pause deadlines are UTC seconds. There is no console endpoint-admission or fetch
command. Status currently reports `delivery_available: false`. Production
signature admission is checked before account settings open. The test-only
`--fixture-updates` path exists only in the isolated fixture executable.
