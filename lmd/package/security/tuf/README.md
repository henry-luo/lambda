# Lambda package-update verifier

Authority: **D11.2.3–D11.2.4**. This is a Lambda Script implementation of the
authentication workflow in [TUF 1.0.37](https://theupdateframework.github.io/specification/latest/).
It is a component under qualification, not a qualified update service or a
claim of TUF conformance. No Rust or Python runtime is required.

Building the signature primitive uses the host mbedTLS 3 / mbedCrypto dependency.
Deployment packages must build that dependency for their advertised minimum OS;
the local Homebrew archive currently targets a newer macOS than Lambda's
configured link minimum, so passing on this machine does not qualify older
macOS deployment. Linux/Windows builds and hardened publisher-qualified
verifier launch need their own qualification.

`lambda.security.tuf` handles bootstrap fingerprints, old-and-new root
thresholds, recomputed key fingerprints, sequential rotation, timestamp/snapshot rollback and freshness,
targets metadata, content hashes and consistent-snapshot filenames.
`lambda.security.tuf.json` reads strict UTF-8 JSON without converting signed
integers to floating point, rejects duplicate decoded keys, and serializes
OLPC canonical signing bytes. `json.document()` separately emits strict JSON
for host handoff, preserving integer lexemes and escaping control characters.
Integer versions have no machine-integer limit.
Flat token/container scans use local loops (S12.1.2v3), so long wire strings,
whitespace, integers and flat arrays do not consume parser call depth. Actual
nesting remains bounded to 64 levels.
`lambda.security.tuf.delegation` validates delegated authority and matches
paths/hash prefixes. The update machine performs an ordered depth-first search,
checks every delegation edge, skips ancestry cycles and honors terminating
roles. Signature math alone uses the native `verify_signature` primitive.

## Trusted host contract

Call `lambda.security.tuf.update.begin_refresh(cache, utc, target_path)` with
the protected current role set, one host UTC start time and an admitted public
target name. Each entry is `{role: "root", bytes: binary_data}`. The root must
already have been explicitly initialized from the admitted out-of-band SHA-256
fingerprint. Cached roles and verifier state must never come from app code,
ordinary files, environment variables or remote responses.

Passing `null` for `target_path` requests metadata-only refresh. Its terminal
status is `refreshed` or `unchanged`; neither authorizes installation. A trusted
caller can then use `select_target(session, target_path)` repeatedly against the
same fixed update time and snapshot. The current production host bridge still
requires an admitted nonempty target and does not expose this API to apps.

Execute `session.action`, then pass its result to `advance(session, response)`:

| Action | Host obligation | Response |
|---|---|---|
| `checkpoint` | Recheck the live job and atomically publish the complete role set, expected generation and trusted clock under the protected settings lock. Acknowledge only after durable publication. | `{status: 'ok'}` |
| `fetch` | Fetch `path` relative to the admitted repository; clamp `limit` to host budgets; enforce HTTPS, origin/peer restrictions, data minimization and live cancellation. | `{status: 'ok', bytes: binary_data}`; `'not_found'` only for an actual HTTP 404/410. Other failures abort. |
| `done` / `unchanged` | Finish the refresh without installation. | No further advance. |
| `done` / `refreshed` | Finish metadata-only refresh; retain the verified session for target lookup. | No further advance; optionally call `select_target`. |
| `done` / `verified` | Recheck live policy before handing the verified bytes and description to the confined staging/installer pipeline. | No further advance. |

The first checkpoint records time before network work. Every accepted root is
checkpointed before the next root fetch. Timestamp or snapshot key changes
discard both cached roles in that same atomic checkpoint. Timestamp, snapshot,
targets and delegated roles are each checkpointed before subsequent fetches.
An equal timestamp resumes target verification using the trusted old timestamp
envelope, so a failed/interrupted artifact download remains retryable. It cannot
extend expiry or change references. Passing `true` as the fourth
`stop_on_unchanged` argument opts into timestamp-only polling; it is not evidence
of an installed artifact.
Unchanged polling still checks the old trusted timestamp's expiry. Cached
metadata is reusable only after checking current signed references, signatures,
expiry, rollback history and the current delegation edge. Invalid cache bytes
fall back to a bounded fetch without deleting rollback history. Valid cache
reuse still requires an acknowledged checkpoint before later work.
Only missing next-root
metadata ends root rotation; malformed data, signature errors and other fetch
failures abort. An expired old root can rotate, but the final root must be fresh.

`host/safety/update_metadata` supplies the bounded opaque checkpoint mechanism:
one profile/authority-bound record per package, generation comparison, persisted
clock watermark, atomic full-role replacement and live-policy leases. It does
not interpret metadata or verify signatures. There is no app/worker API to it.
`host/safety/update_refresh` now connects these actions to bounded downloads and
expected-generation checkpoints. It launches the dedicated immutable verifier
image, checks its sandbox before passing the cache, and monitors process budgets.
The source admission allows only ten embedded files: the eight-file verifier
closure and the two-file [offline package decoder](../Stage.md). It has no app filesystem,
network, grant-store or update-control client. Its private versioned channel is
separate from app IPC. The host clamps signed response limits and debits one
aggregate transfer budget, including redirects. Every action/handoff rechecks
the captured live job; an opaque verified artifact binds the random reservation
and cannot be consumed after cancellation or finish/rebegin.

The owned TLS/early-bootstrap composition control passes on macOS arm64. It uses
an explicitly unhardened ad-hoc fixture; production admission still requires a
hardened Apple-issued publisher and never accepts that fixture. Successful
publisher launch remains unqualified, respecting skipped signing-identity setup.
An offline bounded ZIP decoder now has separate component qualification;
the native adapter now stages opaque verified artifacts into a protected inactive
image with identity-bound cleanup. Managed-image publication, activation/recovery,
installed-catalog coordination and full conformance remain open. The current
console reports delivery unavailable.

## Initial interoperability profile and limits

Supported keys are RSA-PSS/SHA-256 and ECDSA P-256/SHA-256 (named-curve SPKI,
DER signature). Ed25519 keys contribute no valid signatures; Ed25519-only trust
roots must be explicitly replaced/pinned before use with this build. Key IDs
must equal SHA-256 of the canonical key object; arbitrary key aliases are
refused. Metadata uses strict integer JSON and UTC `YYYY-MM-DDTHH:MM:SSZ` timestamps. Repository
role names are opaque printable UTF-8 values up to 128 bytes, stored inside one
protected record rather than used as filesystem paths. Role URLs encode the
whole name as one component, including separators, `%`, `?` and `#`.
Target references accept UTF-8/space and canonical existing URI escapes, with
no empty/dot segments, C0/C1 controls or raw URL-control syntax. The encoder
preserves existing escapes exactly once. Native transport independently checks
uppercase escapes, UTF-8, controls and literal dot segments before URL parsing;
escaped unreserved aliases and encoded authorities are refused. Admitted origins
and base directories retain their plain exact profile. Encoded separators stay
URL data and never become filesystem path syntax. The
current delegation matcher supports `*` and `?`, with no slash matching;
bracket patterns and succinct-role delegations fail closed. Other algorithms
and optional TUF extensions need separate
profile/conformance work. These restrictions are stricter than general TUF.

The machine bounds a refresh to 32 root advances and 64 visited roles. The
host checkpoint holds at most 64 roles and 1 MiB total, with roots up to 64 KiB
and other roles up to 256 KiB. Artifact requests are capped at 64 MiB before
the host's potentially smaller limits. Excess work, unsupported data and missing
checkpoints fail closed. Host process CPU/memory/deadline limits are also required.
Protected disk is the restart authority; a privileged filesystem rollback or
system-clock attack is not solved by a hardware monotonic counter here.

## Verification

`test/lambda/safety_signature`, `safety_tuf_json`, `safety_tuf`,
`safety_tuf_update`, `safety_tuf_delegation`, `safety_tuf_json_document` and
`safety_tuf_json_long` and `safety_tuf_path` are golden-driven tests.
`test_security_update_gtest` covers protected checkpoint/control behavior.
The public fixture generator uses OpenSSL as an independent test signing
oracle; Python and the OpenSSL CLI are test tools only. See
[fixture provenance](../../../../test/safety/tuf/README.md).
`python3 -O test/safety/test_update_pull.py --verifier` exercises the actual
embedded interpreter, protected cache and owned TLS transport, including retry
from saved state and cancellation during verification. This does not qualify
production publisher launch or installation.

The [official conformance runner](../../../../test/safety/tuf/Conformance.md)
pins the upstream suite, real fake-clock tool, executable and source closure.
It currently passes 112/119 checks and returns nonzero: six optional signature
schemes and one historical non-SemVer repository
remain unsupported. No expected-failure file hides them. The deterministic
strict-JSON differential corpus passes 688 inputs on interpreter and JIT. These
are separate from native host/confinement and deployment qualification.
`python3 -O test/safety/test_tuf_paths.py` also checks 728 URI inputs against
Python's standard-library codec across the native host gate, interpreter and
JIT. Native store checks preserve distinct Unicode labels across reload and
verify that traversal-shaped role labels leave another protected record intact.
