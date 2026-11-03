# Inactive package decoder

Authority: **D7.5.2, D7.5.8**. `lambda.security.package_stage` implements the
initial ZIP image policy in Lambda Script. It parses captured bytes using the
existing bounded native archive reader (**S12.1.1v3, S14.3.1v3**), validates the
complete namespace, then emits exact member bytes. It executes no package code,
install hook or nested archive. It has no filesystem-write or network authority.

`begin(bytes)` returns a session whose `action` is either an `entry` or `done`.
After consuming an entry, call `advance(session)` for the next action. Entries
carry `path`, `directory`, `executable` intent and binary `bytes`. Empty members
retain an empty binary IPC carrier despite the ordinary empty-member `null`
convention (**S2.2.2v2**). Completion carries the archive SHA-256, emitted entry
count and total file bytes. These are consistency evidence, not publisher trust.

| Initial profile | Ceiling or rule |
|---|---|
| Format | ZIP stored/deflate, including the reader's validated ZIP64 support |
| Archive / total expanded bytes | 64 MiB each |
| Single member / archive index | 16 MiB / 8 MiB |
| Entries / path depth | 4,096 including implicit directories / 32 |
| Path / component bytes | 1,024 / 255 |
| Names | Printable ASCII; reject Windows forbidden characters/device names, trailing dot/space, empty/dot segments and internal Lambda storage names |
| Namespace | Reject ASCII case collisions, duplicates and file/directory conflicts |
| Objects | Regular files and directories; refuse links, special entries and privileged mode bits |

ASCII is an explicit initial **image-member** restriction. TUF role labels and
target references retain their UTF-8 support. Unicode image names need a
filesystem-qualified normalization/case policy before this profile is widened.
The archive reader rejects traversal/absolute names before construction and
canonicalizes redundant separators and `.` segments; collision checks apply to
the resulting complete namespace. Member CRC, size and expansion checks occur
before emitting that member. A later member failure can leave an incomplete
stream, which must never become an active image.

The immutable offline runtime admits ten embedded source files: the eight-file
verifier closure plus this module and its fixed `_stage_host.ls` entry. The
`lambda-package-stage-v1` process entry uses the dedicated versioned update
channel with `STAGE_INIT`, `STAGE_ENTRY`, fixed-size `STAGE_ACK` and `STAGE_END`
messages. The native
bridge checks types, exact bounded integer conversion, per-message allocation
limits, session and sequence numbers. It waits for an empty successful host
acknowledgement after every entry. Denied/malformed/replayed acknowledgements,
channel closure or deadline expiry abort. No app-worker opcode exposes this
service.

The device-name restrictions include console handles described by Microsoft's
[CreateFile contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea#consoles).
This naming profile does not qualify Windows process confinement or installation.

`host/safety/update_stage` now connects the reservation-bound opaque verified
artifact to protected **inactive** staging. Its production entry admits the
immutable publisher-qualified decoder and enforces the same bounded process
monitor as refresh. An independent native gate checks every received path,
case/duplicate collision, parent type, entry/member/aggregate limit and terminal
artifact digest/count. No app can supply a destination or construct a verified
artifact. The test-only artifact factory is absent from production targets.

Staging is rooted in the held OS-account profile at
`~/.lambda/package-staging/s-<random-id>`, outside user projects, grants and app
data. Only one pending image is admitted per profile, preventing abandoned jobs
from accumulating unbounded stages. The decoder receives bytes and IPC only;
the host creates mode-0700 directories and mode-0600 files through strict
same-device anchors, create-only atomic writes and full-sync barriers. Archive
executable bits remain receipt intent and grant no execution authority.

Each bounded local write holds a live-job lease, serializing with acknowledged
pause/disable/offline changes. No settings lock crosses network, decoding or
IPC waits. The host records the artifact, authority, reservation, directory
identity and description fingerprints in a checksummed intent, plus a binary
receipt of exact paths, flags, sizes and content hashes. A ready intent is
published only after the process monitor accepts a clean exit and policy is
rechecked. Failed or uncertain writes return failure and leave inactive data.
`security_update_staged_info` rechecks the live reservation, held image,
unchanged intent and receipt hash; it returns metadata, not activation authority.

Closing the report releases its live claim and retains the inactive image.
Trusted inspection refuses live claims. Explicit cleanup takes the inspected
random ID, remains available while paused/offline, and removes at most 64
objects per call. A durable discard intent survives each bounded step and host
restart. Stale IDs cannot delete a newer stage. Empty interrupted initialization
is recoverable; unknown orphan payload, corrupt intents, links, special objects
and substituted image identities fail closed. Cleanup never selects app-private
data or user projects. This is staging recovery, not active-version recovery or
a corrupt-record repair interface.

Authentic archive bytes and a host receipt still require managed-image admission
before installation. The installer must validate its package/code/publisher and
capability contract, recheck current trust/policy, publish a crash-safe version
selection, and pin live native versions (**D7.3.2, D7.5.8**). The receipt is not an
independent publisher signature over the extracted file table. Activation,
installed-catalog coordination, image/admission format and associated restart,
revocation and power-loss qualification remain open. Hardened publisher launch
is unqualified with signing-identity setup skipped by user request. Delivery and
default sandbox rollout remain disabled.

Qualification: [owned offline fixture](../../../test/safety/test_package_stage.py),
[signed HTTPS/verifier/staging composition](../../../test/safety/test_update_stage.py),
[fixture provenance](../../../test/safety/stage/README.md), and the progress
record in [the implementation plan](../../../vibe/impl/Lambda_Impl_App_Safety.md).
