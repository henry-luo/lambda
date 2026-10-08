# Lambda I/O ZIP: Archive Input and Output

> **Status:** IMPLEMENTED (2026-10-08), Phase 1 and Phase 2. ZIP32/ZIP64
> input captures an immutable archive and validated index; decompression,
> CRC validation, and member parsing are lazy under **S12.4.1v2/S14.3.1v2**.
> ZIP output supports archive trees and constructed filesystem elements.
> Implementation and verification: [`impl/Lambda_Impl_IO_Zip.md`](impl/Lambda_Impl_IO_Zip.md).
>
> **Scope:** standard ZIP containers, including ZIP-backed document/package
> files. Phase 1 provides read-only `input()` support; Phase 2 provides `output()`.
> Passwords and encryption are excluded from both phases. Other archive
> formats, including 7z, are future work.
>
> **Spec linkage:** §3–§4 → **D7.4.5v2** (virtual element/array carriers),
> **S8.1.2v2** (element iteration), **S2.5.7v4** (`content` returns an array),
> **S2.2.4** and **S2.6.2–S2.6.4** (empty files and element content);
> §5 → **S12.4.1v2**, **S14.3.1v2** (eager archive capture, lazy member
> decompression, and resource closure);
> §6 and Appendix A → **D7.1.2v2**, **D7.5.2** (I/O ownership and policy),
> **D4.1.3**, **D4.2.4**, **D5.3.3** (memory ownership and precise roots);
> §7 → **S10.1.4**, **S12.1.1v2** (explicit procedural output).
>
> **Related design:** [`Lambda_IO_File.md`](Lambda_IO_File.md),
> [`Module_IO_File.md`](Module_IO_File.md),
> [`Lambda_Design_Type_Virtual.md`](Lambda_Design_Type_Virtual.md), and
> [`Lambda_Type_Binary.md`](Lambda_Type_Binary.md).

## 1. Goal and compatibility

An archive should be usable as a directory of files without extracting it to
the host filesystem. Ordinary ZIP files and files whose actual container is
ZIP use the same backend. Member files can contain text, structured documents,
images, or arbitrary binary data.

For example, a Word `.docx` exposes `[Content_Types].xml`, `_rels/`, `word/`,
and its other package entries. This is container access, not conversion of a
Word document into a rendered document or a new semantic Word model. Microsoft
documents `.docx` as a ZIP package; the legacy `.doc` format is a different
binary format and is outside this proposal. [Microsoft's Open XML package
description](https://learn.microsoft.com/en-us/archive/msdn-magazine/2006/november/basic-instincts-server-side-generation-of-word-2007-docs).

The existing filesystem proposal already describes an `<fs>` element with
metadata attributes and a `content()` child axis. ZIP input should reuse that
shape and its file-content machinery, rather than introduce `<zip-entry>`
records, a path-to-bytes map, or another filesystem data model.

There is one deliberate change in direction: the earlier
`Lambda_IO_File.md` proposal kept ZIP-tree handling outside `input()`. This
ZIP-specific extension supersedes that restriction; the filesystem record now
points here and preserves the earlier wording in its superseded appendix.
Ordinary file and directory input results remain compatible.

| Operation | Current or previously proposed behavior | This proposal |
|---|---|---|
| `input(ordinary_file)` | Eagerly parsed root | Preserve the result. |
| `input(directory)` | Eager array of direct-child `Path` values | Preserve compatibility; do not silently replace it with a node. |
| `fs(path)` | Proposed filesystem `Velmt` factory | Reuse its node contract and archive backend when implemented. |
| `input(zip_file)` | Previously no ZIP-tree input backend | Return an archive-backed filesystem `Velmt`. |
| `input(zip_backed_document)` | Previously no generic package-tree input backend | Return the same archive-backed tree, regardless of the outer extension. |

Alignment therefore means **one node contract for the proposed filesystem
tree**, shared metadata spellings, and one member-file read/parse path. It does
not claim that today's `input(directory)` already returns a `Velmt`.

## 2. Supported archive profile

Phase 1 supports **single-disk ZIP32 and ZIP64**, with both Stored (method 0)
and DEFLATE (method 8) members required. ZIP64 support is user-confirmed
(2026-10-08; **S12.4.1v2**). Standard comments, bounded extra fields, and data
descriptors are supported; an archive with no entries is valid. ZIP support
must not depend on a `.zip` extension. These choices refer to the container
and method definitions in [PKWARE APPNOTE 6.3.10,
§4](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).

| In Phase 1 | Outside the initial profile |
|---|---|
| Single-disk ZIP32 and ZIP64 | Split/multi-disk and self-extracting archives |
| `.zip` and ZIP-backed files such as `.docx`, `.xlsx`, `.pptx`, `.epub`, and `.jar`, when their entries use supported methods | Semantic import of the outer document/package format |
| Files, explicit directory entries, implicit parent directories, and empty members | Symlink following, device entries, or other special filesystem objects |
| Stored and DEFLATE, including mixed-method archives | Other ZIP compression methods |
| UTF-8 names and the standard legacy filename encoding | Guessing arbitrary system code pages |
| Read-only in-memory traversal | Extraction, archive mounting, in-place editing, and streaming archive handles |

Encryption is explicitly unsupported: reject encrypted members or encrypted
archive metadata with an unsupported-encryption error. There is no password
option, password prompt, partial decrypted view, or external unzip fallback.
Split/multi-disk and self-extracting archives are also deferred. 7z, RAR, tar,
and standalone gzip streams do not enter this ZIP parser.

ZIP64 is part of the supported standard ZIP format. Resolve its end record,
locator, and extended-information fields wherever ZIP32 fields use sentinel
values, including small members written with ZIP64 local headers. Preserve
64-bit counts, lengths, and offsets throughout validation and indexing, with
checked conversion at allocation/API boundaries. The same I/O and expansion
quotas apply to both ZIP32 and ZIP64; format support does not imply unlimited
memory. The public node model is identical for both variants.

## 3. Phase 1 public surface

Use the existing format-selection convention:

```lambda no-run
// no-run: requires the external assets.zip/report.docx/download.bin files
let archive = input("assets.zip")^
let package = input("report.docx")^
let forced = input("download.bin", 'zip')^
let configured = input("assets.zip", {type: 'zip'})^

let children = content(archive)^
for (entry in children) {
    {name: entry.name, kind: entry.kind, size: entry.size}
}
```

`input()` continues to accept existing string, symbol, and `Path` source
specifiers. ZIP-backed binary **files** are required; an overload accepting a
`binary` value directly is not required for Phase 1. A future in-memory parse
entry point can share the same byte-buffer backend.

### 3.1 Detection and explicit formats

1. An explicit `'zip'` format requires a valid supported ZIP container.
2. An explicit non-ZIP format remains authoritative. In particular,
   `input(path, 'binary')` requests the outer bytes, not the archive tree;
   this raw-byte path must be available for both ordinary files and members.
3. In automatic mode, inspect bytes and validate ZIP structure. Extensions
   and ZIP-derived MIME types are hints; a short `PK` prefix is not proof of
   a valid archive. Empty archives must also be recognized.
4. A confirmed ZIP uses the archive backend even when named `.docx`, another
   package extension, `.bin`, or no extension. Do not route Office MIME types
   into the generic text fallback.
5. A ZIP-expected source that is corrupt or unsupported produces an error,
   rather than being reinterpreted as text after ZIP validation fails.

Raw-byte selection is a shared file-I/O requirement, not a ZIP-only spelling.
New options, including any exposed limits, follow **S17.8.1**: unknown names
in a visible literal are compile-time errors; unknown names arriving through
a value produce a warning and are ignored.

### 3.2 Navigation and member input

`content(archive)^` and `content(directory)^` return a read-only `VArray` of
direct child filesystem nodes. A file member can be passed to `input()` as a
filesystem-node source, using the same format selectors as a local
file. Passing a directory member to this new overload is a type/source error;
its children are obtained through `content()`.

```lambda no-run
// no-run: requires an external report.docx package
let archive = input("report.docx")^
let word = [for (entry in content(archive)^ where entry.name == "word") entry][0]
let part = [for (entry in content(word)^ where entry.name == "document.xml") entry][0]

let document = input(part, 'xml')^
let original_bytes = input(part, 'binary')^
```

Names are ordinary string values: `[Content_Types].xml`, spaces, Unicode, and
punctuation require no identifier conversion. Named element access remains
attribute access; `archive.word` does not secretly become a child lookup.
Likewise, do not introduce a `zip://` Path scheme or a string syntax that
concatenates the outer filename and member path.

Traverse `content(node)`, not `for (entry in node)`: **S8.1.2v2** makes direct
element iteration include attribute values before children. The array result
also follows **S2.5.7v4**, so zero or one child does not collapse its kind.

## 4. Shared file/directory data model

Under **D7.4.5v2**, a `Velmt` presents the semantic kind `element`, with a tag,
named attributes, and ordered children. Its child view presents the semantic
kind `array`. ZIP support adds a backend, not a new language type.

Use the stable `fs` tag from `Lambda_IO_File.md`:

```text
<fs name: "report.docx", kind: 'file', format: 'zip', size: 18432, ...>
  <fs name: "[Content_Types].xml", kind: 'file', size: 1420, ...>
    ...ordinary parsed XML content...
  </fs>
  <fs name: "word", kind: 'dir', size: null, ...>
    <fs name: "document.xml", kind: 'file', size: 9321, ...>
      ...ordinary parsed XML content...
    </fs>
    <fs name: "media", kind: 'dir', size: null, ...>
      <fs name: "image1.png", kind: 'file', size: 5320, ...>
        ...binary content...
      </fs>
    </fs>
  </fs>
</fs>
```

This is an illustrative expanded view; constructing the archive does not
construct every child wrapper or parse every member.

### 4.1 Metadata

| Attribute | Contract |
|---|---|
| `name` | Leaf name, using the same meaning as filesystem nodes. |
| `kind`, `is_file`, `is_dir` | The outer archive is physically a file; member directories are directories and member files are files. Directory-like archive content does not change the outer file's kind. |
| `path` | Outer source `Path`, when available. For a member, it remains the archive's outer path; it is not a fabricated host-filesystem member path. |
| `entry_path` | ZIP-specific, normalized root-relative member name using `/`; `""` at the archive root. Together with the retained archive snapshot, identifies a member. |
| `extension` | Leaf extension, following ordinary file conventions. |
| `size` | Outer archive byte length; uncompressed member-file byte length; `null` for member directories. Never a recursive directory total. |
| `modified`, `mode` | Available source/entry metadata; `null` when unknown or synthesized. ZIP timestamps must not imply timezone precision absent from the source. |
| `is_link` | No following of archive links; the initial profile rejects special/link entries explicitly. |
| `format` | `'zip'` at the archive root, identifying its content backend. |
| `compressed_size`, `compression`, `crc32` | Optional ZIP-specific member metadata; keep it distinct from ordinary `size`. CRC-32 is an integrity check, not an authenticity guarantee. |

The common attributes must have one definition shared with local filesystem
nodes. ZIP-specific attributes extend that contract; they do not reinterpret
existing fields. Member metadata comes from the retained archive index and
never triggers host filesystem `stat()` calls.

### 4.2 Directory and file content

The archive root and every member directory expose direct child nodes through
the same content operation. Synthesize missing intermediate directories so
an archive containing only `word/media/image1.png` still has the expected
three-level hierarchy. Explicit empty directories remain visible.

Child order is first appearance in the central directory. An
implicit directory occupies its first descendant's position; a later explicit
directory record supplies its metadata without adding a duplicate child.
Repeated observations of the same archive value preserve order.

For member files, reuse the ordinary file-content pipeline: obtain exact
uncompressed bytes, validate integrity, then use the existing parser selected
by the member name or explicit format. Supported structured formats produce
their ordinary roots; text produces text; unrecognized binary members remain
binary. Package XML files with names such as `.rels` must be eligible for
content-based XML detection, not only extension-based selection.

`content(file)^` exposes the parsed content under the shared filesystem
contract and ordinary element-content normalization (**S2.6.2–S2.6.4**).
An empty member remains an existing childless filesystem node, following
**S2.2.4**'s empty-file principle; it does not disappear from its directory.
Explicit `input(member, format)^` returns the parser's root directly and
preserves the distinction between an empty source and a non-empty source whose
parsed value is `null`. Raw reads preserve all non-empty payloads exactly;
zero-length binary results follow **S2.2.2v2**, rather than inventing a new
empty-binary value.

Retain original member bytes separately from parsed content. Reading XML or
JSON must not lose the byte payload needed for later binary access or Phase 2
round trips. Parsing failures must not make raw member bytes inaccessible.
Default member-content parsing keeps nested ZIP payloads as binary. An explicit
`input(member, 'zip')` opens a nested archive; listing an archive never
recursively expands archives.

## 5. Input evaluation, snapshots, and lifetime

**S12.4.1v2** requires `input()` to capture and index the complete archive,
close its source within the call, and return a pure value. **S14.3.1v2** makes
member decompression lazy. The user-confirmed evaluation boundary is:

1. Read the complete outer archive into an owned, immutable byte snapshot
   through the ordinary I/O boundary, enforcing the input-byte limit while
   reading. Close its source before returning.
2. Validate container structure and index the entire central directory before
   returning the root. Validate member locators, supported features, local
   header consistency, and declared resource limits at this point.
3. Back directory views with that compact index. Create `Velmt` wrappers only
   when accessed; no eagerly built recursive Lambda tree is necessary.
4. On member-content access, decompress from the retained bytes, check actual
   size and CRC, and parse as requested. Cache each successful result or
   terminal failure for repeat access.

Constructing the root, listing or indexing directory children, querying child
counts, and reading metadata perform **no member decompression or payload CRC
validation**. Only a member-content operation such as `content(file)^` or
`input(member, format)^` forces that member's payload. Stored members follow
the same access-time integrity checks even though they need no decompression.

There is no deferred filesystem read, open ZIP handle, memory mapping that can
observe later source writes, or hidden retry against a changed source. A member
or child array retained after the root becomes unreachable must still retain
the backing snapshot. Re-reading the source with `input()` creates a fresh
value; it does not mutate an earlier archive.

Eagerness here means acquiring and validating the complete container snapshot;
it does not mean expanding member bodies. Decompression, payload integrity
errors, and parsing occur only at member-content access. This boundary is
recorded in **S12.4.1v2/S14.3.1v2**; eagerly decoding every member is
superseded (§9 and Appendix B).

The filesystem proposal follows ordinary element normalization: a parsed
`null` produces no child under **S2.6.2**, while explicit member input still
returns that parser root.

## 6. Validation, errors, and limits

Archive navigation is read-only and never extracts files. Treat entry names
as archive-local names, not paths to open on the host. The index should:

- Reject absolute, drive-qualified, UNC, NUL-containing, and `..` traversal
  names. Require ZIP's `/` separator instead of accepting backslash aliases.
- Decode names consistently: UTF-8 when declared, validated Unicode-path
  metadata when present, otherwise the standard legacy encoding. Detect
  collisions after decoding and normalization; keep names case-sensitive and
  avoid host-platform case folding or Unicode normalization.
- Reject duplicate file entries and file/directory conflicts. Merge a single
  explicit directory record with an already synthesized parent directory.
- Check every offset, length, and arithmetic operation against the actual
  snapshot. Validate the end record, central directory, local headers, and
  data-descriptor relationships; never search compressed bodies for record
  signatures to infer boundaries.
- Reject unsupported methods, encryption, multi-disk records, and special
  filesystem entries explicitly. Do not silently omit them from the view.

Bound archive bytes, index bytes, entry count, name length, hierarchy depth,
per-member expanded bytes, and total expanded bytes. Enforce actual expansion
as well as declared sizes; cache eviction must not reset an archive's expansion
budget. Explicit nested archive input inherits a bounded nesting/expansion
policy. Concrete defaults and option names belong to the shared I/O quota
design under **D7.5.2**, not an unlimited ZIP-only bypass.

Use the normal `T^E` error mechanism for unreadable input, malformed ZIP,
unsupported features, integrity failure, decoding failure, and exceeded limits.
Include the outer source and member name where applicable, without logging
payloads. Container/index failures occur at `input()`; deferred payload failures
occur at the selected member-content boundary under **S14.3.1v2**.
An error is never an empty directory, missing child, `null`, or truncated file.

Virtual dispatch must preserve these errors. `content(FsNode)` and member
input need correct error/effect typing, and generic child/count/query/format
paths must not discard `VIRTUAL_OP_ERROR`. Since indexed ZIP directory counts
are known at input time, obtaining their length needs no further I/O; that does
not remove the shared virtual-protocol error obligations for file content.

## 7. Phase 2: `output()`

Add ZIP writing through the existing `output(data, target, format/options)`
door. **S10.1.4** makes `output` the explicit file-write operation, and
**S12.1.1v2** places writes in procedures.

```lambda no-run
// no-run: source_tree is supplied by the caller
pn save_archive(source_tree) {
    output(source_tree, "copy.zip", 'zip')^
}
```

The writer consumes the same logical file/directory tree as input: archive
roots, directory roots, or equivalent materialized filesystem elements. It
walks directory children, writes file payloads with binary-safe lengths, and
preserves empty files and explicit empty directories. A filesystem backend
adapter must obey the same node contract; a second ZIP-only manifest is not
the primary API.

For an unmodified input tree, use original member bytes. For newly constructed
or changed files, accept exact binary/text payloads or an explicitly selected
existing formatter. Arbitrary structured values must not be silently coerced
to text; the accepted tree shape and serialization choice must be validated
before writing. Raw payload access must not depend on successfully parsing a
member.

Emit the same accepted ZIP profile, with Stored and DEFLATE, CRCs, central
directory records, and no encryption. `output(tree, "copy.zip")` may infer ZIP
from the extension, following the ordinary output convention. A package-like
target such as `copy.docx` requires explicit `'zip'`; writing a ZIP container
does not guarantee a valid Word document, package relationships, signatures,
or application-specific metadata.

Rebuild the archive as a whole. Append mode and in-place entry mutation are
outside this phase. Use the shared checked/atomic file-write facility so a
payload, formatting, or encoding error does not publish a partial replacement;
all output resources close on success or failure. Phase 1 remains read-only.

Round-trip success means the same member names, logical payload bytes, and
supported metadata. Synthesized parent directories may be written as explicit
directory records. It does not promise identical compressed bytes, record
ordering beyond the chosen traversal order, or preservation of every unknown
ZIP extra field. Timestamp and compression policy must be explicit enough to
allow deterministic output when requested.

## 8. Delivery and acceptance

Phase 1 delivers explicit and automatic ZIP32/ZIP64 input, the shared
filesystem `Velmt`/`VArray` view, lazy member decompression and read/parse
support, snapshot ownership, errors, and quotas. It does not depend on
completing local filesystem tree migration or output support.

Acceptance coverage should include:

- Stored, DEFLATE, mixed methods, comments, extra fields, data descriptors,
  empty archives, empty files/directories, and omitted parent directories.
- A real ZIP-backed Word fixture, including XML parts and binary media;
  renamed `.bin` and extensionless copies; explicit ZIP and raw-binary modes;
  a legacy `.doc` rejection case.
- Nested traversal, special-character and non-ASCII names, stable child order,
  metadata meanings, explicit member parsing, and binary payloads containing
  NUL bytes. Cover ZIP32 and ZIP64, including forced ZIP64 on small members,
  64-bit metadata boundaries, and large entry counts without truncation.
- Corrupt offsets/headers/CRC, truncated input, duplicate/conflicting names,
  traversal attempts, unsupported methods, encryption, special entries, and
  declared/actual quota violations, including nested archive budgets.
- Source replacement/deletion after input, a retained member after dropping
  its root, repeated reads, forced GC, and parity across T0 and MIR tiers.
- Opening, listing, counting, indexing children, and reading metadata perform
  zero member decompressions. Accessing one file decodes only its payload;
  repeat access reuses the cached result. A corrupt member CRC is reported
  only when that member's payload is forced, not during a directory listing.
- Existing ordinary file/directory results and the Lambda baseline remain
  unchanged. Each new golden-driven `.ls` fixture has its matching `.txt`.

Phase 2 adds independent-reader interoperability, input/output member-payload
round trips, constructed trees, empty entries, binary-safe output, deterministic
options, failure cleanup, and rejection of append mode. Performance evaluation,
if undertaken, uses a release build and measures index cost and accessed-member
cost separately.

## 9. Confirmed decisions and remaining proposal scope

The user settled both initial questions on 2026-10-08:

1. **Include ZIP64 in Phase 1**, alongside ZIP32 (**S12.4.1v2**).
2. **Decompress lazily on member-content access** after capturing the complete
   archive and validating its index. Listing and metadata reads never force
   payloads (**S12.4.1v2/S14.3.1v2**).

The implementation uses the shared `fs` node contract, `input(file_node,
format)`, exact raw-binary selection, and compatible ordinary directory input.
The general local `fs(path)` factory and live filesystem refresh are separate
work in [`Lambda_IO_File.md`](Lambda_IO_File.md).

### 9.1 Limits and output options

**Z1 — bounded archive policy (D7.5.2, S17.8.1).** The options below are
accepted in input and output option maps. Limits are nonnegative integers;
zero is a real limit. Existing transport limits may be stricter (the synchronous
HTTP adapter also retains its 50 MiB response cap). Index limits include implicit directories and wrapper
metadata. An explicit nested ZIP inherits its parent's budget and nesting
limit; its option map cannot enlarge that budget. Successful and failed
expansions consume the shared ledger, and cached reads do not consume it again.

| Option | Default | Meaning |
|---|---:|---|
| `max_archive_bytes` | 268435456 (256 MiB) | Captured input bytes or encoded output bytes. |
| `max_index_bytes` | 67108864 (64 MiB) | Index and filesystem metadata budget. |
| `max_member_bytes` | 268435456 (256 MiB) | Uncompressed bytes per member. |
| `max_expanded_bytes` | 1073741824 (1 GiB) | Aggregate declared/actual expansion, shared across explicitly opened nested archives. |
| `max_entries` | 1000000 | Entries including synthesized parent directories, excluding the synthetic archive root. |
| `max_name_bytes` | 4096 | Decoded UTF-8 member path bytes. |
| `max_path_depth` | 128 | Member hierarchy depth. |
| `max_nesting_depth` | 8 | Explicit nested ZIP depth; outer archive is depth zero. |

**Z2 — ZIP encoding policy (S10.1.4, S12.1.1v2).** Output options use
`format: 'zip'` (or a `.zip` target), `compression: 'deflate'` (default) or
`'stored'`, `compression_level: -1` (default, or 0–9), `zip64: false`
(default; required ZIP64 records are still emitted automatically), and
`deterministic: false` (default). Deterministic output sorts member paths by
UTF-8 bytes and uses 1980-01-01 00:00:00 floating DOS timestamps. The same
logical input and options produce the same bytes within a codec version.
Ordinary output preserves supported entry modes and DOS timestamps; unknown
timestamps use 1980-01-01. Unknown extra fields, comments, and original
compressed representations are rebuilt.
ZIP publication always uses the shared atomic writer; append mode is rejected.

Constructed trees use `<fs kind: 'dir', ...>` for the root, child directories
with `name` and `kind: 'dir'`, and files with `name`, `kind: 'file'` and
text/binary children. Names are single components, without `/`. A file with
`format: 'json'` (or another supported formatter) carries zero or one value;
multiple structured values without a formatter are rejected. Empty file nodes
encode zero bytes. `mode` optionally supplies permission bits.

```lambda
pn save_constructed_zip() {
    let tree = <fs kind: 'dir',
        <fs name: "hello.txt", kind: 'file', "hello">
        <fs name: "data.json", kind: 'file', format: 'json', {answer: 42}>
        <fs name: "empty", kind: 'file'>
    >
    output(tree, "temp/example.zip", {deterministic: true})^
}
```

## Appendix A. Implemented architecture

- **Acquisition and dispatch:** `lambda/input/input.cpp` acquires exact bytes
  with `lib/file.h`'s bounded reader and detects ZIP before MIME/text fallback.
  Explicit format selection remains authoritative. The same length-bearing
  dispatcher parses member payloads and preserves raw binary bytes.
- **Archive backend:** `lambda/io/zip_archive.cpp` owns the immutable snapshot,
  checked ZIP32/ZIP64 index, CP437/UTF-8/Unicode-extra name decoding, expansion
  ledger, cached DEFLATE/Stored payloads and CRC errors, and ZIP writer. It
  reuses zlib and shared endian/byte-storage/file helpers without vendor edits.
- **Filesystem adapter:** `lambda/input/input-zip.cpp` implements the shared
  metadata contract and `Velmt`/`VArray` operations. Flat child-index spans
  provide O(1) indexed navigation; wrappers and attributes initialize on
  access. Payloads and per-format parse results are cached independently.
- **Lifetime:** **D4.1.3/D4.2.4** Input-owned pool/arena nodes and parse caches
  live until their InputManager is torn down. Pool cleanup releases the archive
  once. A retained child remains valid after dropping the root. Runtime-owned
  generic content views retain/trace their owner with **D5.3.3** precise roots.
- **Virtual errors:** virtual ABI 2 adds an optional fallible `prepare` hook
  before cached `count`. Content, indexing, iteration, query and formatting
  preserve failures. `len()` keeps its ordinary `int` contract; deferred host
  failures travel through the **S7.4.3/D6.1.3** defect channel, remaining
  catchable with `or` without changing ordinary count annotations. Jube ABI 9
  rejects modules compiled against the earlier callback layout.
- **Output:** `lambda/runtime/lambda-proc.cpp` selects ZIP through the ordinary
  output door; the I/O adapter validates the tree and encodes all bytes before
  publication. `write_binary_file_atomic` closes/checks the temporary file and
  renames it only after successful encoding. Generic structured output also
  completes fallible formatting before opening its target.
- **Interactive browsing:** `lambda view archive.zip` (including ZIP-backed
  DOCX/JAR and renamed ZIP sources) and `lambda view directory/` share the
  bundled `lambda.doc.doc_viewer` tree (**D7.2.4**). Its path-source entry
  captures the root once; rows retain member nodes for expansion and preview
  (**S12.4.1v2/S14.3.1v2**). Structured/text/binary previews read those nodes;
  images use data URLs and HTML uses `srcdoc`, without extracting members or
  inventing host member Paths. Diagnostic printing treats virtual carriers as
  opaque, so UI logging does not force member content. Archive-relative linked
  resources are not mounted as host files or given a new URL scheme.

## Appendix B. Superseded proposal choices

Superseded by the user's 2026-10-08 decisions (§9):

- ~~The proposed minimum is ZIP32; including ZIP64 in Phase 1 is an open
  scope choice.~~ Both ZIP32 and ZIP64 are now required in Phase 1.
- ~~Member decoding timing remains open. The strict alternative decompresses
  and CRC-validates every member, with default parsing completed before
  `input()` returns.~~ The complete archive snapshot and index are eager;
  member decompression, CRC validation, and parsing occur only on access
  (**S12.4.1v2/S14.3.1v2**).
