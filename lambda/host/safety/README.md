# Host and worker safety components

The broker-side components implement **D7.5.2, D7.5.5–D7.5.8**.
`grant_store.cpp`, `file_broker.cpp`, `private_storage.cpp`, `admission.cpp` and `supervisor.cpp`
now belong to the dedicated host executable and isolated qualification fixtures;
they are absent from worker images. The same host now includes protected package
update controls and console management; delivery is not yet implemented.
Host ad-hoc admission binds source identity,
populates a base catalog and restores supported grants. An experimental macOS
supervisor verifies and launches the offline T0 worker, handles once-only terminal
consent, and monitors resources. Default CLI integration, certificate-backed
release packaging and the complete installed-app/private-directory catalog
remain pending.

`worker.cpp` is the experimental runtime bootstrap for a fresh worker process.
It installs the fixed native process policy before runtime initialization, then
binds its IPC client before canonicalizing, reading, parsing or initializing
the entry. The bootstrap captures its launcher-selected working directory
before confinement and copies that lexical metadata into a context capsule.
Runner, T0 and task path conversion use it without native getcwd or chdir.
Directory metadata grants no file authority. It does not admit grants. The dedicated supervisor now verifies launch and binds grants;
installed publisher qualification and default CLI integration remain pending.
Ordinary CLI execution retains its existing unbound model.

`update_download.h` is a trusted updater component under **D7.5.2,
D7.5.7–D7.5.8**, linked only by host/test targets. It constructs public
repository-relative requests from a live admitted job, rechecks exact origins
on bounded redirects and delegates raw HTTPS to `lib/https_pull`. Fresh handles
exclude browser state, ambient credentials/proxies and CA-file overrides;
actual socket peers must pass a public-address filter. Header/body/deadline
limits and policy cancellation apply before returning any complete bytes.
Only a short final handoff holds a publication lease. The result is unverified
metadata/artifact bytes, with no parser, extraction, install hook or activation.
The host has no fetch CLI and still reports delivery unavailable. An isolated
fixture binary alone supplies loopback resolution/test roots; production has
neither its entrypoint nor its override. See the TLS recorder in
[`test_update_pull.py`](../../../test/safety/test_update_pull.py).

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
actual Lambda entry/import/data/output/path/task behavior under the experimental
native process policy and compares native syscall attempts with an unsandboxed
control. Consent there
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
runtime. The host and current experimental worker have no entitlements; an
App Sandbox entitlement is rejected because it prevents applying the stricter
process policy. An unsigned/ad-hoc host refuses launch.
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
process reaping. Reaping happens once while remaining stdout/stderr chunks
continue to drain; read failures retain their errno. The OS receives only `PATH` and `LANG` from the launcher.
No account/container `HOME` is supplied to this process-policy worker. The broker endpoint is
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
real `~/.lambda/setting`. Current process-policy tests do not need OS app
containers; the original App Sandbox probe remains historical evidence.

This does not complete **M2 / D7.5.1, D7.5.5–D7.5.7**. The private Darwin
interface is an experimental candidate, approved by the user on 2026-11-03 as an experiment;
qualification is required before any default rollout.
Complete catalog/installed-app admission, persistent root
bookmarks, shipped-resource mapping, full
race/crash/debug/mapping coverage, kernel memory/thread/storage policy and
certificate-backed deployed qualification remain required. No default sandbox
or package-update service is shipped by this component.

Platform references: [Apple static code validation](https://developer.apple.com/documentation/security/secstaticcodecheckvalidity(_:_:_:)),
[validation flags](https://developer.apple.com/documentation/security/static-code-validation-flags),
[spawn attributes](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/posix_spawnattr_setflags.3.html).



## Broker-owned private data, cache and temporary files

`private_storage.*` implements the host service under **D7.5.2, D7.5.5–D7.5.7**.
The following virtual mounts are independent of CWD and project selection. They
use the existing broker channel; its admitted principal selects the physical
owner. A worker cannot name another owner or receive a native directory handle.
Private replacement/append need no project-overwrite prompt. All three mounts
refuse source and canonical-source acquisition, before physical source lookup.

| Worker mount | Lifetime and default fallback quota |
|---|---|
| `/.lambda/app-data` | Persistent per principal; 64 MiB, 1,024 entries |
| `/.lambda/app-cache` | Persistent, discardable per principal; 32 MiB, 1,024 entries |
| `/.lambda/app-temp` | Separate files per admission; 16 MiB and 512 entries across all that principal's live sessions |

Physical files are beneath `~/.lambda/app-data/{a|s}-<principal-digest>/`.
Data/cache use `data/` and `cache/`. Temporary files use
`temp/r-<128 random bits>/files/`; the virtual temp mount hides the session name.
Each temp session holds an exclusive `.lease` lock for its lifetime. No PID or
caller-supplied identity selects cleanup authority. At most 16 sessions are live
per principal; internal session directories/lease files consume its temp entry
quota. Data/cache quotas are shared by independently opened host instances too.

Owner records bind the principal, storage class and namespace/owner/directory
identities. Existing data's record remains compatible. A corrupt or incomplete
data owner record fails closed; trusted repair is still pending. Incomplete
cache/temp mount initialization without a published record is discarded under
the owner lock, never adopted. Mismatched records fail closed. All anchors
require private owners, trusted parents, no links and one device. The main
catalog excludes the whole physical namespace even through broad grants.

Admission's generation lease guards every callback and response publication.
An identity-checked owner lock serializes bounded accounting and mutations;
contention refuses without mutation, never waiting under a grant lease. Writes
charge bytes and missing parent entries across the selected class before
mutation. Strict traversal rejects forbidden entries instead of hiding them.
The worker's native policy prevents direct filesystem writes. Atomic staging
can temporarily consume an additional bounded write payload; retained staging
files count in later scans. Per-request/session limits also apply.

`security_private_storage_clear_cache` is trusted host eviction, absent from
worker IPC. It retains data and active temp trees; authorized live clients may
repopulate the cache. `security_admission_finish` runs after all borrowers and
dispatchers join, revokes the session and removes its temp tree. The supervisor
reports `cleanup_error` and refuses a success result if removal fails. Close
retries bounded cleanup and logs deferred removal; it never follows an alias.

On the next admission, the owner lock protects a bounded temp-session scan.
A busy lease preserves the live session. An acquirable lease, including a session
interrupted before lease/files creation, permits anchored cleanup before a new
random session is created. A killed host releases its lease in the kernel;
no timeout or PID reuse can make a live session eligible. A worker crash or
watchdog kill reaches the same supervisor teardown. Cleanup rechecks object
identity, uses distinct REMOVE guards, and syncs parent directories after unlink.
Unsafe or oversized retained trees fail closed for trusted repair. This covers
process death, not every power-loss/filesystem-corruption case.

Under **D7.5.9**, uninstall retains persistent data by default. The installer
must expose deletion as a separate explicit action; installed-app lifecycle
integration remains pending. Host-only `security_private_storage_inspect`
reports bounded class usage, quota excess, live/abandoned session counts and
pending-reset state without creating an admission or recovering abandoned temps.
It publishes no partial result on failure. A management scan has a hard
4,097-entry bound per class even if the configured write quota is lower: up to
4,096 quota entries plus an extra internal staging entry left by interruption.

`security_private_storage_reset_begin` requires explicit trusted consent and no
live session. It preflights storage and durably publishes a random reset ID bound
to the principal and namespace/owner/data objects before any data deletion.
`security_private_storage_reset_step` checks that same ID, refuses live sessions
and removes at most the requested number of entries. Every unlink syncs its
parent; completion removes the intent only after the data tree is empty. The
data directory/owner record remain in place. New admissions refuse while an
intent exists, and corrupt or replayed records fail closed. A killed manager
leaves a resumable intent; ordinary launch never resumes deletion. Cache,
temp, grants, updater preferences and other principals are preserved. A failed
sync can follow a committed mutation: inspect before recovery, never blindly
start a new reset. This is not secure media erasure or corrupt-owner repair.

The experimental verified host console exposes:

```sh
./lambda-safety-host.exe storage status script PRINCIPAL_HEX
./lambda-safety-host.exe storage reset-data script PRINCIPAL_HEX --confirm-data-loss
./lambda-safety-host.exe storage resume-reset script PRINCIPAL_HEX RESET_HEX --confirm-data-loss
./lambda-safety-host.exe storage quota script PRINCIPAL_HEX GENERATION data BYTES ENTRIES
./lambda-safety-host.exe storage quota-defaults script PRINCIPAL_HEX GENERATION
```

Use `app` instead of `script` for an app principal. The principal must already
have a protected grant-store record; these remembered records identify a
management target and never restore execution authority. `status` supplies the
pending reset ID, if any. Each reset/resume requires the explicit deletion flag.
These commands remain behind the existing publisher-signature gate; no signing
setup is attempted after the user's skip request. No management opcode or symbol
is added to the worker. Graphical quota UI, uninstall integration,
persistent-data repair and full power-loss/deployment qualification remain open.

Quota overrides live in protected `setting/storage-{a|s}-<principal>.quota`
records under **D7.5.2, D7.5.7**. A typed principal-keyed state slot constructs
these names; it accepts no arbitrary path and cannot select grant or updater
records. The fixed 160-byte record contains the principal kind/digest, profile
digest, generation, custom/default mode, three byte/entry limits and integrity
checksum. Profile identity uses the protected store's existing identity helper.
Custom policy replaces the config's trusted fallback limits. Default-mode
records retain a generation and use the current host fallback; they encode no
old fallback values. Corrupt, substituted or cross-profile records fail closed.

`status` reports effective limits, `quota_generation` and `custom_quota`. Copy
the generation into the edit/default command. `quota` changes one class in the
current policy; use `cache` or `temp` in place of `data` above. Bytes must be a
positive uint64; entries must be 1–4,096, or 3–4,096 for temp's internal
session/files/lease bookkeeping. The 16-session ceiling and per-request/session
budgets remain separate. Editing refuses live leases or pending data reset;
stale generations fail without publication. Default restoration writes a new
generation rather than deleting the record. Short protected-state transactions
end before traversal/cleanup, and neither opening nor editing may run while the
caller holds a store lease.

Admission acquires quota policy under its owner lock before creating a session.
It therefore either captures the acknowledged policy or prevents an edit with
its live lease. Finished clients refuse IO, so they cannot retain old limits
after an edit. Quota reductions preserve existing files and report over-limit
usage. A replacement that brings total bytes and entry count into quota is
permitted; unsafe unsigned subtraction is avoided when retained bytes or entries
already exceed the limit. Data reset preserves policy and generations. Missing
policy uses the fallback; corrupted policy cannot be reset through defaults or
ordinary launch. Installed lifecycle and graphical quota UI remain pending.

The protected settings/profile namespace and private brokers now use
`FILE_ANCHOR_STRICT_STORAGE`: private ownership, trusted parents, one device and
full storage syncing (**D7.5.2, D7.5.7**). Directory creation syncs child then
parent before returning an anchor. Create-mode retry resyncs retained edges;
read-only acquisition does not sync. Creating a regular lock/lease syncs the
empty file and parent before exposing its stream; later content needs its own
write/sync. A failed create sync can leave the object and must not be treated
as permission to replace it. macOS additionally requires `F_FULLFSYNC` after
`fsync` for these creations, atomic/in-place writes and unlink publication.
Failure propagates without fallback. A failed post-commit barrier retains the
existing uncertain-commit result; protected state marks the store unavailable.
The mounted filesystem accepts the full-sync operation in current native
controls. Device honesty, physical power loss and corrupt-owner recovery remain
separate qualification gates.

## Experimental native process policy

`lib/process_sandbox.*` applies a fixed deny-by-default policy before untrusted
work. The profile allows read/map access to immutable `/System/Library` and
`/usr/lib`, random bytes, and an open of the root directory alone for libuv's
EVFILT_USER placeholder. It grants no user-file reads or disk writes, network,
fork/exec, preferences or named Mach-service access. Existing native file
descriptors and mappings are not revoked by this policy: native controls can
still read and obtain a shared writable mapping through a pre-held file fd.
Before runtime initialization, the trusted worker therefore requires one
bootstrap thread and exactly four descriptors: stdin is the actual `/dev/null`
device, stdout/stderr are anonymous pipes, and the broker is a connected AF_UNIX
stream. Extra descriptors, high-numbered handles, redirected stdio, named FIFOs
and pre-existing threads refuse startup before broker IO. A fixed metadata-only
allowance for `/dev/null` permits identity revalidation in policy-first native
fixtures; it grants no device data access. Broker IPC and captured stdio remain
usable. The host requires kernel sandbox-label readback before
any broker callback can run. Readback is not policy authentication: sealed
publisher/image validation and the exact trusted bootstrap establish the policy.
There is no weaker fallback if policy installation fails. Darwin requires an
explicit `iokit-get-properties` denial in addition to its legacy `iokit*`
group; native controls exercise model/unique-ID reads and device-client opens
using both fresh and pre-held rights. No identifier value is retained.

The backend uses private Darwin `sandbox_init_with_parameters`/`sandbox_check`
interfaces, as in [Chromium's macOS seatbelt implementation](https://chromium.googlesource.com/chromium/src/+/main/sandbox/mac/seatbelt.cc).
Native evidence currently covers macOS 26.5.2 arm64 only. This is an explicit
platform experiment, not adoption of a private API for production or completion
of M0/M2. The user approved the experiment on 2026-11-03; default rollout remains gated
on qualification.
The normal CLI remains unbound. Debug/task-port, mappings, remaining hardware
and malicious-worker/crash coverage still need qualification.

Under **D7.5.1–D7.5.2 / D7.5.5–D7.5.7**, the launcher now separately clears
registered Mach send rights, task exception ports and the bootstrap port through
spawn actions before child startup. FD `CLOEXEC` does not close these resources:
[XNU explicitly inherits them](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/ipc_tt.c).
The registered-port action uses all `TASK_PORT_REGISTER_MAX` empty slots; an
empty action list preserves inheritance. Its runtime-resolved
`posix_spawnattr_set_registered_ports_np` SPI is part of the experimental backend
and missing support returns `ENOTSUP`, without launching a weaker worker.

The native inheritance fixture plants parent send rights and a writable Mach
memory entry. Both its unsandboxed control and a sandboxed child launched without
Mach cleanup can send parent messages and modify that fixture page. The actual
launcher must prevent both, with the rights already absent before bootstrap.
The worker additionally rejects `SYS_ptrace` through the OS `syscall-unix`
filter: [XNU's `PT_TRACE_ME` path checks the parent's credentials](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/mach_process.c),
so neither the worker's ordinary default-deny policy nor hardened signing alone
proved that denial. The rule follows the syscall filters in the installed Apple
system profiles and requires native version qualification.

`TASK_ACCESS_PORT` remains present and retrievable on this OS. Clearing it via
spawn fails with `EINVAL`; a tested special-port policy did not prevent its
acquisition. The Mach gate retains that failure. Parent task-name acquisition
has a successful unsandboxed control and is denied under the policy; full task
control is denied even in that control, so it does not qualify foreign-task
debugger isolation. A separate self-target `task_for_pid` control succeeds and
is refused under the worker policy; its JSON records that entry-point result
without treating it as the missing foreign-task positive control.
These results do not complete the kernel-port/debug audit or M2.

### Early runtime loading experiment

Under **D7.5.1–D7.5.2 / D7.5.5–D7.5.7**, native constructors must also run
inside the boundary. The direct-linked worker fails the constructor gate even
with a clean descriptor table: a constructor can retain a file mapping after
closing its fd. No VM metadata allowlist is treated as proof that prior file
contents were never acquired.

`lambda-safety-bootstrap.exe` and `LambdaSafetyRuntime.dylib` are an opt-in split
build. The bootstrap links only libSystem; it captures cwd and validates handles
before applying the fixed process policy. It then grants read/map authority to
the one canonical sealed runtime path and loads its versioned entry. Runtime
and dependent framework constructors therefore execute under the policy. It
neither permits process exec nor loads an app-selected native image. Shared
handle/endpoint mechanisms are separate C files so they do not pull host or
framework initializers into the bootstrap.

The runtime entry uses kernel label readback as a necessary check. The exact
policy is established by the sealed bootstrap chain, not by that readback or a
caller-provided flag. Bundle admission now checks nested code, and hardened
library validation remains enabled. The production supervisor can verify and
launch the sealed split bundle; successful hardened loading and deployment
remain unqualified. Local unhardened controls are never production admissions.
Signing-identity setup is skipped by the user; no default rollout is enabled.

This follows [Chromium's minimal-helper / delayed-framework design](https://chromium.googlesource.com/chromium/src/+/main/sandbox/mac/seatbelt_sandbox_design.md),
which identifies native framework initializers as a reason to sandbox before
loading the runtime.

A per-principal ad-hoc re-signing prototype was rejected: a re-signed copy with
the same bundle ID and an allowed actual parent could read another container's
file. The earlier apparent denial depended on an invalid parent constraint.
The counterexample and retired source are retained under
`temp/app-safety-rejected-container-design/`; claimed bundle IDs are not app
principal authentication. Current storage access is bound to the broker channel,
without using claimed OS container names.


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
The separate `update_download` component supplies a bounded HTTPS pull, returning
unverified bytes. The Lambda Script verifier and `update_refresh` coordinator
authenticate those bytes; production publisher launch, conformance, installed
catalog integration, staging and activation remain M3u work. No update command or request-now handle exists in worker
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
host spellings, IP literals and known local hostnames. The separate transport
checks resolved public peers and each redirected origin. Public-path semantics and
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
This is not hardware-backed anti-rollback storage. The separate metadata cache
below persists verifier checkpoints. Reservations round persisted UTC seconds
upward while the exact monotonic budget starts before durable publication;
another host cannot steal a live ticket on the next clock tick. Protected
recovery and complete updater deployment remain outstanding.

`security_update_job_enter` revalidates a ticket and holds one bounded local
publication lease. A control change in another host cannot acknowledge until
that earlier publication releases its lease. Release on the acquiring thread;
never wait for network, extraction, UI, or call another control API while held.
The current transport checks dispatch, transfer and final handoff; future
verification/installers must also check staging and activation and cancel on
refusal. Reserving a job alone does not grant a fetch or authorize
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

`update_metadata` implements opaque per-package verifier checkpoints under
**D7.5.7–D7.5.8**. Initialization is explicit/create-only and matches the root
bytes to the admitted out-of-band SHA-256 fingerprint. Full-role replacement
is atomic, generation-checked and serialized with live policy using the same
settings lock as the update catalog. No lock is held while downloading or
checking signatures. Profile and admitted endpoint/channel/origins/publisher/
bootstrap identity bind each record. Scheduling preferences do not change that
binding. A persisted trusted-clock watermark refuses backward clock movement;
live controllers additionally detect observed generation rollback or replacement
at the same generation. These are protected-file controls, not hardware-backed
rollback protection against a privileged disk restore.

The record holds at most 64 named roles, 1 MiB total, with roots limited to
64 KiB and other roles to 256 KiB. Names select public repository roles, never
arbitrary settings paths. Missing/corrupt/identity-mismatched state refuses the
captured job. Read/restore never initializes trust. Native storage treats all
metadata/version fields as opaque bytes; Lambda Script decides authentication,
versions, expiry and root-rotation cache deletion. Checkpoint publication is not
permission to stage or install.

`update_refresh` executes the [Lambda verifier action contract](../../../lmd/package/security/tuf/README.md)
in a dedicated early-confined T0 process built by `make build-update-verifier`.
The immutable ten-file source manifest (eight verifier files and two offline
decoder files) is embedded at build time and read
through a source-only client. Other IO is refused. No settings/control service
is linked into that runtime; a separate versioned, sequence/session-bound IPC
channel carries bounded cache, fetch, checkpoint and verified-result messages.
The production entry requires a sealed hardened Apple-issued publisher image,
an actual sandbox readiness check and CPU/memory/wall/output budgets.

The coordinator records time before parsing/network work, applies generation-CAS
checkpoints, debits an aggregate response budget including redirects, and rechecks
the live job throughout transfer and handoff. Verified bytes are opaque and bind
the random persisted reservation, so pause/disable/offline or finish/rebegin
refuses later views. A returned view is data; installation still requires a live
publication lease. No lock is held across VM execution or network work, and no
updater operation was added to the app worker protocol.

`test_update_pull.py --verifier` passes the owned TLS/real offline-worker
composition control, including saved-cache retry and cancellation. Its ad-hoc,
unhardened fixture is not production launch admission. Successful hardened
publisher loading remains unqualified with identity setup skipped by user
request. Conformance, managed-image admission/publication, activation/recovery and installed
catalog coordination remain open; delivery stays disabled.

Under **D7.5.2, D7.5.8**, the same offline runtime has the fixed
`lambda-package-stage-v1` decoder entry. Its [Lambda Script policy](../../../lmd/package/security/Stage.md)
validates the complete bounded ZIP namespace, then emits exact regular-file or
directory records through a separate stage operation sequence. Only a fixed-size
empty `STAGE_ACK` advances it; limits are checked before frame allocation.
The decoder cannot write files, acquire network content or execute package code.
Its terminal digest/counts prove stream consistency, not installation authority.
The trusted adapter now consumes live opaque verified artifacts and writes only
into a protected, inactive anchored stage. The owned 53-case decoder fixture is
an unhardened composition control, not publisher/deployment qualification.

`update_stage` applies a second native namespace/type/size gate and a live-policy
lease before each create-only full-sync write. The fixed OS-account profile
namespace admits at most one pending image; neither callers nor the decoder
select destinations. A random ID, exclusive claim, checksummed intent and exact
content receipt bind it to the reservation and held directory identities. The
shared `update_process` monitor must accept clean exit before final ready
publication and a fresh policy check. Receipt views verify reservation, image,
intent revision and receipt hash; no app opcode exposes these operations.

Closing the handle retains inactive bytes and releases its claim. Trusted
inspection/identity-bound cleanup work offline/paused, refuse live or substituted
objects and remove at most 64 entries per step. Intent survives partial cleanup;
unknown initialization debris and corrupt records are quarantined. App-private
data remains untouched. The anchored tree primitives were extracted from
`private_storage` into `storage_tree` without changing its cleanup policy.
Active image selection, installed catalog/publisher/capability admission and
active-version recovery remain separate pending gates; delivery stays disabled.

Under **D7.5.2, D7.5.7–D7.5.8**, cached role names are bounded opaque UTF-8
labels inside the existing record, with exact byte identity and unchanged v1
layout. They never select settings filenames. The verifier encodes each role
as one URL component. Native transport independently validates canonical path
escapes and UTF-8 before parsing; encoded authority syntax, controls, escaped
unreserved aliases and literal traversal are refused. Encoded separators remain
data. Origins and admitted bases stay plain and exact. Target references keep
existing canonical escapes once. This adds no app opcode or filesystem grant.
