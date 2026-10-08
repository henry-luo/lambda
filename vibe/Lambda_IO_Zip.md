# Lambda I/O ZIP: Archive Input and Output

> **Status:** PROPOSAL (2026-10-08), not implemented or ratified. The requested
> direction is ZIP input through `input()`, a directory-like `Velmt` view,
> alignment with file/directory I/O, and later ZIP output. API details and the
> questions in §9 remain proposals; this document changes no formal ruling.
>
> **Scope:** standard ZIP containers, including ZIP-backed document/package
> files. Phase 1 is read-only `input()` support; Phase 2 adds `output()`.
> Passwords and encryption are excluded from both phases. Other archive
> formats, including 7z, are future work.
>
> **Spec linkage:** §3–§4 → **D7.4.5v2** (virtual element/array carriers),
> **S8.1.2v2** (element iteration), **S2.5.7v4** (`content` returns an array),
> **S2.2.4** and **S2.6.2–S2.6.4** (empty files and element content);
> §5 → **S12.4.1**, **S14.3.1** (eager input and resource closure);
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

There is one deliberate change in direction: `Lambda_IO_File.md` §2.2 and §9
keep ZIP-tree handling outside `input()`. This proposal requests a ZIP-specific
extension to `input()` and records that earlier restriction for reconciliation
when accepted. It does not change ordinary file or directory input results.

| Operation | Current or previously proposed behavior | This proposal |
|---|---|---|
| `input(ordinary_file)` | Eagerly parsed root | Preserve the result. |
| `input(directory)` | Eager array of direct-child `Path` values | Preserve compatibility; do not silently replace it with a node. |
| `fs(path)` | Proposed filesystem `Velmt` factory | Reuse its node contract and archive backend when implemented. |
| `input(zip_file)` | No ZIP-tree input backend | Return an archive-backed filesystem `Velmt`. |
| `input(zip_backed_document)` | No generic package-tree input backend | Return the same archive-backed tree, regardless of the outer extension. |

Alignment therefore means **one node contract for the proposed filesystem
tree**, shared metadata spellings, and one member-file read/parse path. It does
not claim that today's `input(directory)` already returns a `Velmt`.

## 2. Supported archive profile

The initial proposed profile is single-disk ZIP32, with both Stored (method 0)
and DEFLATE (method 8) members required. Standard comments, bounded extra
fields, and data descriptors are supported; an archive with no entries is
valid. ZIP support must not depend on a `.zip` extension. These choices refer
to the container and method definitions in [PKWARE APPNOTE 6.3.10,
§4](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).

| In Phase 1 | Outside the initial profile |
|---|---|
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

**ZIP64 is an open scope choice (§9).** It is an extension within the standard
ZIP format, not another archive family like 7z. The ZIP32 baseline follows the
earlier filesystem proposal; if ZIP64 is deferred, recognize and reject it
explicitly instead of truncating its counts, lengths, or offsets.

## 3. Phase 1 public surface

Use the existing format-selection convention:

```lambda no-run
// no-run: proposed ZIP dispatcher and filesystem-node input support
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
proposed filesystem-node source, using the same format selectors as a local
file. Passing a directory member to this new overload is a type/source error;
its children are obtained through `content()`.

```lambda no-run
// no-run: proposed filesystem-node input overload and ZIP content backend
let archive = input("report.docx")^
let word = first(content(archive)^ that ~.name == "word")
let part = first(content(word)^ that ~.name == "document.xml")

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

Proposed child order is first appearance in the central directory. An
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

**S12.4.1** requires `input()` to close its resources within the call and
return a pure value. **S14.3.1** calls input eager. The proposed approach is:

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

There is no deferred filesystem read, open ZIP handle, memory mapping that can
observe later source writes, or hidden retry against a changed source. A member
or child array retained after the root becomes unreachable must still retain
the backing snapshot. Re-reading the source with `input()` creates a fresh
value; it does not mutate an earlier archive.

**The timing in step 4 remains open, not a claimed exception to the formal
specs.** It defers decompression, integrity errors, and default parsing, even
though all external I/O has completed. §9 asks whether this interpretation of
eager archive input is acceptable. The strict alternative decompresses and
CRC-validates every member before returning, with any required default parsing
also completed then; wrappers and child arrays can still be virtual over those
already computed results.

Before implementation, settle this boundary and reconcile the filesystem
proposal's file-content wording with ordinary element normalization. In
particular, its §4.2 promise of a child containing a parsed `null` must not
override **S2.6.2**. If acceptance changes a formal ruling, revise that ruling
in place with its version suffix and the spec semver, update the relevant
filesystem record, and regenerate the formal index.

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
occur at the selected member-content boundary if §5's timing is accepted.
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
// no-run: proposed ZIP output formatter; source_tree is an existing fs tree
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
supported metadata. It does not promise identical compressed bytes, record
ordering beyond the chosen traversal order, or preservation of every unknown
ZIP extra field. Timestamp and compression policy must be explicit enough to
allow deterministic output when requested.

## 8. Delivery and acceptance

Phase 1 delivers explicit and automatic ZIP input, the shared filesystem
`Velmt`/`VArray` view, member read/parse support, snapshot ownership, errors,
and quotas. It does not depend on completing local filesystem tree migration
or output support.

Acceptance coverage should include:

- Stored, DEFLATE, mixed methods, comments, extra fields, data descriptors,
  empty archives, empty files/directories, and omitted parent directories.
- A real ZIP-backed Word fixture, including XML parts and binary media;
  renamed `.bin` and extensionless copies; explicit ZIP and raw-binary modes;
  a legacy `.doc` rejection case.
- Nested traversal, special-character and non-ASCII names, stable child order,
  metadata meanings, explicit member parsing, and binary payloads containing
  NUL bytes. ZIP32/ZIP64 behavior must match the resolved scope.
- Corrupt offsets/headers/CRC, truncated input, duplicate/conflicting names,
  traversal attempts, unsupported methods, encryption, special entries, and
  declared/actual quota violations, including nested archive budgets.
- Source replacement/deletion after input, a retained member after dropping
  its root, repeated reads, forced GC, and parity across T0 and MIR tiers.
- Existing ordinary file/directory results and the Lambda baseline remain
  unchanged. Each new golden-driven `.ls` fixture has its matching `.txt`.

Phase 2 adds independent-reader interoperability, input/output member-payload
round trips, constructed trees, empty entries, binary-safe output, deterministic
options, failure cleanup, and rejection of append mode. Performance evaluation,
if undertaken, uses a release build and measures index cost and accessed-member
cost separately.

## 9. Open questions

1. **How eager must ZIP input be?** Recommended: capture bytes and validate the
   complete index during `input()`, then decompress/CRC-check and parse member
   content on access without further external I/O (§5). Alternative: complete
   all member decoding and integrity checks, plus default content parsing,
   before returning. This decision must agree with **S12.4.1/S14.3.1**; the
   draft does not silently revise them.
2. **Include ZIP64 in Phase 1?** The proposed minimum is ZIP32, matching the
   earlier filesystem proposal. ZIP64 is standard ZIP and useful for large
   archives or many entries; supporting it now avoids an additional format
   limitation. Either choice must be stated in the advertised support profile.

The remaining surface choices are concrete recommendations for review:
reuse `fs` nodes, add `input(file_node, format)`, provide shared raw-binary
selection, and keep `input(directory)` compatible. They are not additional
ratified rulings.

## Appendix A. Implementation notes

- **Source acquisition:** `lambda/input/input.cpp:2212`,
  `input_from_local_path`, currently selects a binary reader specially for
  PDF and otherwise computes length with `strlen()`. ZIP and member parsing
  require a shared length-bearing byte-source path, using existing
  `lib/file.h` helpers and `input_from_source_n`; adding only an extension
  check for `.zip` or `.docx` leaves the underlying binary-input defect.
- **Dispatch:** `lib/mime-types.c` already contains ZIP signatures and Office
  package MIME names; `lambda/input/input.cpp:2015`, `mime_to_parser_type`,
  still has a generic text fallback. Reuse detection, add validated archive
  dispatch, and preserve explicit format precedence. Use one container parser
  for local sources, member snapshots, and any existing length-safe transport
  adapter; new transport support is not required.
- **Directory compatibility:** `lambda/input/input-dir.cpp:23`,
  `input_from_directory_with_name_parent`, builds child `Path` values today.
  Share the proposed filesystem-node factory without changing that legacy
  return shape as a side effect of ZIP support.
- **Virtual access:** `lambda/runtime/lambda-eval.cpp:7292`, `fn_content`,
  currently admits concrete elements only. Extend the generic element-family
  path once for filesystem backends; implement member-source resolution in
  the shared input dispatcher rather than copying parsing logic into ZIP.
- **Layering and codecs:** archive indexing and format adaptation belong in
  `lambda-io` under **D7.1.2v2**; raw acquisition stays behind **D7.5.2**.
  Reuse the existing zlib dependency for raw DEFLATE and CRC support. A ZIP
  library, if selected during implementation, needs no vendor-source edits;
  codec choice does not change the public tree contract.
- **Lifetime:** retain source/index and parsed `Input` owners explicitly.
  Pool/arena storage follows **D4.1.3/D4.2.4**; GC-managed wrappers, cached
  Items, and native helper temporaries use precise trace hooks and
  **D5.3.3** `RootFrame`/`Rooted` ownership. An arena buffer is not a GC
  object, and tracing a raw pointer is not a substitute for retaining its
  actual owner. Do not retain a filesystem descriptor or rely on native-stack
  scanning to keep member data alive.
