# Bounded brokered local fetch

Status: design, 4 November 2026. The accounting bank CSV importer reads a
bounded, user-selected local file through `io.fetch`, but a runtime with a
security I/O binding currently refuses all fetches. Scoped SQLite is also not
yet admitted in that runtime, so this change alone will not qualify the app as
a sandboxed deployment.

## Narrow first milestone

Permit a bound `io.fetch` only for a local regular-file read with an explicit
positive `max_bytes` and no other options. Preserve the existing response map
and content classification, including the byte count and the empty-body rule.
Reject a remote or unsupported target and every other fetch option before any
file or network operation. In particular, never configure `to`, resume,
headers, method, body or callbacks through a path that could open a file or
create a directory before checking authority. Existing unbound behavior remains
unchanged. A bound `io.fetch` without the explicit bound continues to fail
closed. This narrow subset does not imply that network fetch, streaming,
progress callbacks or arbitrary file operations are supported in a bound app.

Resolve the local target using the existing target conversion, then call the
existing `security_io_read_bytes` with data-read authority (`source=false`),
the requested size ceiling and a finite transport ceiling. Do not acquire
metadata or use an ambient fallback if a client is bound. The broker enforces
the effective grant on the resolved target and the size limit while reading;
transport failures, invalid clients, revocation and oversized data are errors.
Path conversion and `file:` parsing must not turn a remote authority or alias
into a local path that escapes broker checks. D7.5.2 and D7.5.5 require the
central I/O door and admitted resource authority; a path selected by app code
is not itself a grant. Retain precise managed roots and independent ownership
of native bytes across offload, cancellation and error cleanup (D5.3.3).

Before implementation verify the existing channel's maximum payload, client
lifetime and thread-safety for parked calls, and whether it can distinguish an
over-limit response from a complete short read. If any guarantee is missing,
fix the broker/transport contract at its source or fail closed; do not truncate
or use a check-then-read workaround. Do not present transport cancellation as
proof that the host stopped reading. Never return bytes after a denied or
revoked acquisition.

## Acceptance

- Both tiers: admitted local reads produce the same response and byte count as
  unbound reads; exact limit succeeds, one byte over fails, and Unicode, empty,
  binary and invalid-UTF-8 content preserve existing behavior.
- Denied file, missing grant, sibling-prefix traversal, symlink escape,
  revocation, missing/invalid client, transport loss and request tampering fail
  without ambient reads. Verify actual OS-confined worker and broker transport,
  not only a mocked client.
- Verify remote schemes, ambiguous file authorities and all unsupported options
  fail before dispatch and before any mutation; retain the existing unsupported
  service checks. Exercise concurrent calls, cancellation and precise GC roots.
- Run the Lambda baseline and security suites, then the accounting CSV read and
  UI workflows against the bound environment when scoped SQLite is available.
  Keep platform, lifecycle and deployment qualification explicit.
