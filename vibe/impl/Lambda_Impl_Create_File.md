# Exclusive file publication for accounting exports

Status: implementation in progress, 4 November 2026. Accounting invoices and
reports need a complete new file without replacing an existing destination.
Expose a distinct `io.create_file(path, bytes)` procedure instead of a new
`output` mode. Existing `output` accepts unknown modes as ordinary write, so
an older runtime could otherwise replace an existing file when an app believes
it requested create-only publication. An older runtime must reject the new
procedure rather than silently downgrade it.
An existing error operand propagates before I/O (S11.4.3).

The first implementation accepts only a local target and a string or binary
payload. An existing parent directory is required. Use the existing held-parent
`file_anchor_write_atomic` create-only primitive: it stages and synchronizes
the bytes, publishes without replacement, validates the held parent and syncs
the directory. Existing files, symlinks and special objects fail; the held
parent is checked before staging and before publication. As the underlying
anchor API documents, those checks are not a compare-and-swap against an
external process that renames an ancestor at the publication instant; use a
stable destination directory and inspect an unconfirmed result. No
existence-check/rename sequence is acceptable. A
return value is the byte count only after durable publication. A post-publication
sync failure must report that publication occurred but durability is uncertain;
the caller must inspect the destination instead of blindly retrying. Unavailable
platform primitives fail closed. This operation is immediate and external to
any surrounding SQLite transaction (S12.4.4v4).

Admitted I/O authority is required for every publication (D7.5.2, D7.5.6).
The initial implementation rejects all bound security clients, because their
current `write` callback cannot express exclusive creation; it must not fall
back to ambient access. A later transport and broker extension must carry an
explicit create-only operation, authorize CREATE, prevent an overwrite approval,
and distinguish rejected, committed-with-error and unknown transport outcomes.
The accounting app must not claim support in a bound runtime until that exists.

Verify interpreter and MIR; text, Unicode, binary and empty contents; existing
files/directories/symlinks; invalid paths and absent parents; competing creators;
parent substitution; injected failure before and after publication; and denial
with a bound client. Test byte equality, no overwrite and transaction rollback
independence. Preserve existing native file and Lambda baselines, reporting
pre-existing failures accurately. The invoice renderer and publication lifecycle
remain separate work and require their own review and accounting tests.

The interpreter and MIR golden passes with forced collection, and the current
26 native anchor tests pass, including injected sync failure, revocation,
ancestor replacement and concurrent creators. The final full Lambda baseline
passed 8,754/8,754, including all 96 app-safety runtime tests and the bound-client
refusal case. Accounting's separate non-GST invoice text-export prototype has
focused UI and storage tests; it does not qualify production invoicing or the
bound-client implementation.
