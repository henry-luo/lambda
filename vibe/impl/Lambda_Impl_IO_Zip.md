# ZIP I/O implementation

> **Status:** DONE (2026-10-08), Phase 1 input and Phase 2 output.
> **Design:** [Lambda_IO_Zip.md](../Lambda_IO_Zip.md).
> **Formal anchors:** **S12.4.1v2/S14.3.1v2** (captured ZIP32/ZIP64 input and
> lazy member decoding), **D7.4.5v2** (virtual carriers), **D7.1.2v2/D7.5.2**
> (I/O layering), **D4.1.3/D4.2.4/D5.3.3** (ownership and rooting).

## Delivered behavior

**Phase 1 — input (S12.4.1v2/S14.3.1v2).** `input()` recognizes standard
single-disk ZIP32/ZIP64 by content, including renamed, extensionless and
ZIP-backed Office/package files. Explicit formats remain authoritative.
Acquisition preserves exact binary lengths for local and HTTP sources, applies
bounded reads, and closes the source before returning an immutable archive
snapshot and validated index. No filesystem extraction is involved.

The reader supports Stored and raw DEFLATE, ZIP64 end/local/central records,
signed and unsigned ZIP32/ZIP64 data descriptors, archive comments, UTF-8,
validated Unicode-path extra fields and CP437 names. It rejects encryption,
split/self-extracting archives, other methods, special/link entries, unsafe
paths, duplicate/conflicting names, inconsistent headers and invalid ranges.
The published limits bound archive/index bytes, entries, names, path depth,
member/aggregate expansion and explicitly opened nested archives.

The shared `fs` contract exposes archive/file/directory `Velmt` nodes and
`VArray` child views (**D7.4.5v2**). Missing parents are synthesized, child order
follows first central-directory appearance, and wrappers initialize on access.
Listing, indexing, counting and metadata access decode no member payloads.
Member access lazily checks expansion size and CRC, then parses as requested.
Bytes, parse results and terminal failures are cached independently; a parsing
failure does not prevent raw binary access. Nested archives remain binary
unless explicitly opened as ZIP.

**Phase 2 — output (S10.1.4/S12.1.1v2).** `output()` accepts archive trees,
filesystem directory/file nodes, child views and equivalent materialized `fs`
elements. Existing members retain their original bytes; constructed files
accept binary/text content or an explicit formatter. Empty files/directories,
Stored/DEFLATE options, supported modes/timestamps, deterministic output and
automatic/forced ZIP64 are implemented. Unknown timestamps use the earliest
valid DOS date, 1980-01-01. Append is rejected. The complete bounded archive is
encoded before the shared checked atomic writer publishes its replacement.
Formatting, payload, quota or encoding failure leaves an existing target intact.

Both phases are complete within the proposal's profile. Other archive formats,
encryption, extraction, semantic Word import and the general local `fs(path)`
factory remain outside this work's scope.

## Implementation and compatibility

| Area | Implementation |
|---|---|
| Container/index/codec | `lambda/io/zip_archive.hpp/.cpp`: immutable byte storage, checked index, shared expansion ledger, lazy payload cache and writer; reuses zlib and shared endian helpers. |
| Filesystem adapter | `lambda/io/fs_node.hpp`, `lambda/input/input-zip.cpp`: common metadata, flat child spans, lazy wrappers, member parsing and output tree validation. |
| Acquisition | `lambda/input/input.cpp`, `input_http.cpp`, `lib/file.c`: length-bearing dispatch, bounded file/cache/download reads, raw binary selection and checked file writes. |
| Runtime | `lambda.hpp`, interpreter, MIR and builtins: virtual preparation/error propagation, member `input`, checked content/count operations and output dispatch. |
| Formatting | Public formatting boundaries preserve deferred virtual failures and finish formatting before opening ordinary output targets. |

Input-owned pool/arena nodes and parsed Inputs follow **D4.1.3/D4.2.4**.
Pool cleanup releases the retained archive once; a child survives after the
root goes out of scope. Runtime-owned generic content views trace their owner
and use precise **D5.3.3** roots. Shared error data lives in the core layer so
the I/O module does not depend on runtime error implementation
(**D7.1.2v2/D7.5.2**).

The optional fallible virtual `prepare` callback advances the virtual ABI to 2
and Jube ABI to 9 (**D7.4.5v2**). All five bundled native modules were rebuilt
with matching manifests. Deferred failures remain visible through content,
indexing, iteration, query and formatting. `len` retains its `int` contract;
lazy host failures use the existing defect channel and compiler `may_defect`
fact (**S7.4.3/S14.3.1v2/D6.1.3v2**). Concrete string/array length paths retain
their native treatment, with physical virtual-array dispatch checked first.
Ordinary file and directory input results are preserved.

Reviewed MIR changes: DeltaBlue adds 14 instructions in `incremental_add`;
Prettier adds 154 across checked lengths and scalar consumers, while its
`print_node` body shrinks by 14. Updated default ratchets record the measured
counts without slack; unmeasured platform budgets were left unchanged
(**D6.1.3v2**).

## Verification

Validation completed on macOS on 2026-10-08:

| Check | Result |
|---|---|
| `make build` | Passed. |
| `make test-lambda-baseline` | **6394/6394**, including input 2104/2104, Lambda 4290/4290, ZIP 13/13, MIR emission 246/246 and MIR ratchet 20/20. |
| Native ZIP suite after the final timestamp change | **13/13**. |
| Four ZIP goldens across interp/auto/jit, with ordinary and forced/poisoned GC | **24/24**. |
| File and endian helper tests | **110/110** and **6/6**. |
| Independent Python `zipfile` reader | Passed CRC, forced ZIP64, original member-byte round trips, constructed binary/text/JSON/empty entries and deterministic timestamps. |
| Local HTTP fixture | Passed fresh Office-MIME download, cached binary read and bounded cache rejection. |
| `make check-doc-code ARGS='--filter Lambda_Sys_Func'` | **56/56** clean units. |
| Formal index and `git diff --check` | Passed; the index checker reports an existing duplicate DO25 warning. |

Native fixtures cover signed/unsigned descriptors, fake EOCD signatures in
comments, CP437/Unicode names, explicit/implicit directory merging, lazy CRC
failures, nested budgets, source snapshot independence, malformed/unsupported
headers, writer validation and the actual 65,535-entry ZIP64 boundary.
`test/input/zip/office.docx` is a minimal Open XML Word package with XML,
relationships, binary media and Unicode/spaced names. Each new Lambda golden
has its matching `.txt`; tests cover retained-member GC lifetime, explicit
parsing/raw reads, iteration/count failures, output round trips and preservation
of existing targets after rejected writes.

The repository-wide `make lint` remains failing on existing findings outside
this implementation: unresolved `lambda_array_rep_cert_resolve` effect
audits, missing DOM editable registry input, Radiant header rules and existing
memory-kind node fields. No ZIP or checked-length finding remains.
`make check-module-boundary` links the strict lib/core/io/radiant DSOs cleanly;
its overall ratchet remains failing on existing deferred RT-to-Radiant imports
(260 against baseline 165). Neither unrelated ratchet was relaxed. Logs and
manual validation scripts are retained under `temp/`.

## JAR regression coverage (2026-10-08)

Added `test/input/zip/sample.jar`, compiled from the checked-in `Hello.java`
with OpenJDK's `javac --release 8` and packaged by its `jar` tool. The fixture
has a runnable `Main-Class` manifest, DEFLATE class/resource entries with data
descriptors, the JAR extra field, JSON/text/binary resources, an empty file and
a Unicode filename. `generate_jar.py` reproduces it using a JDK, with all
scratch files under `temp/`; ordinary regression runs need no Java install.

The native regression verifies zero decodes while indexing/reading metadata,
individual member decoding and cached class reads
(**S12.4.1v2/S14.3.1v2**). Two new goldens cover automatic/explicit ZIP input,
filesystem navigation, manifest text, class-file bytes, resources and explicit
ZIP output to a `.jar` target. Both join the existing tier-parity gate.

- Native ZIP suite: **14/14 passed**.
- Six ZIP goldens across interp/auto/jit, with ordinary and forced/poisoned
  GC: **36/36 passed**.
- Independent Python reader: CRCs and all 11 original/output entries match.
- OpenJDK 25.0.2 runs both the original JAR and Lambda's rebuilt JAR, printing
  `Hello from JAR!`; the application also checks its binary resource bytes.
- Repeated fixture generation with the same JDK produces identical bytes.

No runtime change was needed for JAR support. Reproduction commands and fixture
contents are documented in `test/input/zip/README.md`.

## Interactive archive browsing (2026-10-08)

`lambda view <path>` routes directories and ZIP32/ZIP64 archives through the
bundled `lambda.doc.doc_viewer.open_browser` path transform (**D7.2.4**).
Routing uses the shared extension/signature detector; `input()` still performs
validation and immutable capture (**S12.4.1v2**). DOCX, JAR, renamed and
extensionless archives open the same tree. The existing HTTP probe recognizes
ZIP bytes before staging its local input.

Tree rows retain the actual host Path or archive `fs` node, with display paths
kept separately. Expanding directories enumerates child metadata; selecting a
member requests its content (**S14.3.1v2/D7.4.5v2**). Nested archive rows open
explicitly as ZIP. Member dotfiles remain visible, parse errors preserve raw
source access, and deferred CRC failures appear at the affected row/preview.
Images use existing binary-to-base64 formatting for data URLs; HTML previews
use `srcdoc`. Bundled math assets resolve from Lambda home independently of
the selected archive. Relative links between archive members are not mounted
as filesystem resources or a new URL scheme.

Debug element logging previously traversed embedded virtual nodes and decoded
unopened members while building tree rows. Diagnostic printing now describes
virtual carriers without invoking callbacks, preserving laziness and expansion
budgets (**S14.3.1v2**); explicit value printing still accesses content. A
native regression verifies both behaviors.

Validation:

| Check | Result |
|---|---|
| `make build` | Passed. |
| `make test-lambda-baseline` | **6401/6401**, input 2104/2104 and Lambda 4297/4297. |
| Native ZIP suite | **15/15**, including diagnostic laziness. |
| Viewer CLI regressions | **2/2**, covering directories, ZIP64, empty/renamed/extensionless archives, DOCX/JAR and unsafe archive rejection. |
| Document viewer UI fixtures | **17/17**, including five new archive fixtures with **39/39** assertions for tree navigation, previews, binary source, parse recovery and CRC errors. |
| Archive UI fixtures under forced/poisoned GC | **5/5**, **39/39** assertions. |
| Loopback HTTP DOCX viewer | Passed Office MIME acquisition, ZIP tree dispatch and **9/9** member preview assertions. |

Commands, event fixtures and logs are retained under `test/ui/` and `temp/`.
