# Lambda I/O File: Velmt-backed Files, Directories, and ZIP Archives

> **Status:** PROPOSAL (2026-09-09). This records the agreed direction for
> filesystem values; it does not yet amend the formal specifications or change
> the current `Path`/`input()` implementation.
>
> **Scope:** a read-only Lambda filesystem tree represented by `Velmt` and
> `VArray`: local files, directories, symbolic links, and ordinary ZIP
> archives using DEFLATE. This document does not design write operations,
> streams, a new URL scheme, or ZIP64.
>
> **Formal anchors:** **D7.4.5v2** (Velmt and VArray are virtual forms of the
> element and array semantic kinds), **D7.5.2** (one central I/O door),
> **S8.1.2v2** (element iteration includes attributes before children),
> **S2.2.4** (empty-file content), **S12.4.1** and **S14.3.1** (`input()` is
> eager), and **D5.2.1v3** (precise tracing/rooting).
>
> **Related records:** [`Module_IO_File.md`](Module_IO_File.md) is the
> cross-language `lib/file.*` substrate. [`Lambda_Design_Type_Virtual.md`](Lambda_Design_Type_Virtual.md)
> defines the carrier and vtable model. This record adds the filesystem
> backend; it does not replace either document.

---

## 1. Outcome

Filesystem traversal should be expressed as a tree of ordinary Lambda
elements, backed by virtual carriers rather than materialized arrays or ad-hoc
Path results:

```text
fs(/.assets)                    -> lazy filesystem Velmt
content(node)^                  -> a VArray of direct child Velmts or file content
fs.refresh(node)^               -> a distinct node with a fresh metadata snapshot
```

`fs.node(path)` is intentionally not introduced. `fs(path)` is the one
proposed factory. `content` is the existing child-axis operation, extended to
accept the filesystem Velmt backend; it is not a second `fs.children` API.
`fs.refresh(node)^` is the one proposed new maintenance operation.

The public node is an element semantic value under **D7.4.5v2**, not a fourth
container kind. Its tag is the stable implementation-neutral tag `fs`; its
attributes describe the filesystem object and its children are its content.
Using one stable tag means constructing a node does not need to stat a path
merely to decide whether it is printed as a directory or a file.

```text
<fs name: "assets", kind: 'dir, ...>
  <fs name: "theme.json", kind: 'file, ...>
  <fs name: "icons", kind: 'dir, ...>
</fs>
```

The tree must be traversed through `content(node)`, not directly with
`for (x in node)`: **S8.1.2v2** defines direct element iteration as attribute
values followed by children. `content(node)` selects only the child axis.

## 2. Public surface and compatibility boundary

### 2.1 Proposed system functions

| Surface | Status | Contract |
|---|---|---|
| `fs(path)` | new | Create a lazy filesystem Velmt for a `Path`, string, or symbol. It validates and normalizes the locator but performs no stat, directory scan, file read, parse, or ZIP inspection. |
| `fs.refresh(node)^` | new | Resolve the node's locator anew, follow links, capture a fresh metadata snapshot, and return a distinct filesystem Velmt whose content slot is empty. It does not list a directory, read a file, parse input, or inflate ZIP data. |
| `content(node)^` | existing function, extended | For a filesystem Velmt, establish/access its independent content snapshot and return its VArray child view. Native-element behavior remains the existing read-only view. |

`open()` is not an alternative spelling: **S12.4.1** reserves it for scoped,
procedural resource acquisition. `fs(path)` creates a value descriptor, not an
open handle.

The `^` on filesystem `content` is deliberate. Access may need to open a
directory, read a file, parse a document, inspect a ZIP central directory, or
report an I/O error. A failure must not be coerced to an empty VArray. The
static effect typing of this receiver-sensitive overload is an implementation
gate in §8; the intended user contract is explicit at the call site.

### 2.2 `input()` remains unchanged

`input(path)` remains the eager parse operation required by **S12.4.1** and
**S14.3.1**:

| Existing call | Preserved result |
|---|---|
| `input(file)^` | The eagerly parsed input root. |
| `input(dir)^` | The current eagerly materialized, direct-child `Path` list. |
| `input(zip)^` | The current normal input dispatch/result or error; ZIP-tree behavior is not implicitly enabled through `input`. |

Likewise, existing `Path` concatenation, properties, wildcard behavior, and
iteration stay compatible in the first migration. The existing Path resolver
may later be implemented through this backend only if it materializes its
present results exactly. No source program silently changes from a `Path` list
or parsed root into an FS Velmt.

## 3. Node shape and metadata

### 3.1 Attributes

All filesystem Velmts expose a read-only attribute face. The exact final
surface should retain the useful current Path spellings where possible:

| Attribute | Meaning after the metadata snapshot |
|---|---|
| `name` | Leaf name in the requested parent. |
| `path` | Requested outer `Path`; a ZIP member additionally has an internal normalized entry locator, not a new `zip://` Path scheme. |
| `extension`, `scheme`, `depth`, `parent` | Structural path information. |
| `kind` | Physical resolved kind: `'dir` or `'file`. An archive is physically a file whose content uses the ZIP backend. |
| `is_dir`, `is_file`, `is_link` | Resolved target kind plus the distinct link-identity flag. |
| `size` | File bytes for ordinary files and archive bytes for ZIP files; `null` for every directory. It is never recursive. A ZIP-member file's `size` is its uncompressed logical byte count. |
| `modified`, `mode` | Snapshotted source metadata where the backend/platform supplies it. |

Directory `size` is intentionally `null`. The current Path value exposes raw
`stat().st_size` for a directory, but that is the platform's internal
directory-entry storage length, not a recursive total, child count, or stable
cross-platform file-size meaning. `len(content(dir)^)` is the direct-child
count. A recursive aggregate, if added later, must be a named tree walk rather
than a misleading `size` attribute.

The internal identity of an archive member is the pair
`(outer archive Path, normalized entry path)`. This proposal deliberately does
not create a public `zip://` Path scheme. Archive-member presentation and
addressing can be designed later without redefining the existing Path type.

### 3.2 Attribute/metadata snapshot

Attribute loading is its own snapshot level:

1. `fs(path)` has only a locator and no metadata snapshot.
2. The first named attribute request snapshots metadata for the node.
3. Every later attribute read uses that stored metadata; it performs no new
   filesystem stat or link traversal.
4. The snapshot does **not** enumerate a directory, read a file, parse input,
   or inspect/inflate ZIP content.

The rule intentionally applies even to an attribute that could be derived from
the path, such as `name`. This makes the first attribute read the single,
observable metadata boundary instead of making different fields accidentally
come from different filesystem moments.

Metadata loading uses an `lstat`-then-follow model. The first result preserves
the fact that the requested object is a link (`is_link`); resolution then
follows the link chain to snapshot the target's kind, size, modified time, and
mode. Dangling links and link cycles are I/O errors, not absent attributes.

The metadata cache and the content cache are deliberately independent. A link
or pathname may change between the first attribute read and the first content
read; then attributes report the earlier metadata snapshot while `content`
reports the later content snapshot. `fs.refresh(node)^` obtains a separate
latest node when a caller needs a new metadata observation.

## 4. Content snapshot and lazy VArray

`content(node)^` owns the second snapshot level. It is separate from the
metadata cache and is taken the first time content is requested, whether or
not attributes were previously observed.

The content snapshot may resolve the locator and follow symlinks independently
to determine whether its source is a directory, ordinary file, or a ZIP
archive. It does not overwrite an earlier metadata snapshot. This is the
necessary consequence of the requested two-level design: a content snapshot
is a separately timed observation, not proof that earlier attributes are
still current.

The returned value is a read-only VArray, as required by the virtual carrier
shape in **D7.4.5v2**. A filesystem backend must not build an ordinary Lambda
array merely to simulate laziness.

### 4.1 Directory content

At first `content(dir)^`, the backend opens the resolved directory and creates
a content-VArray state. It does **not** eagerly call `dir_list`, does not stat
every direct child, does not create a whole child list, and does not read any
child content.

`get_at(i)` advances the directory scan only through entry `i`. For every
discovered entry it records the entry name in the VArray's private discovery
cache. The returned child is a fresh filesystem Velmt seeded only with:

```text
parent locator + child name
```

It has no metadata snapshot. In particular, listing a directory never stats a
file merely to determine its size, kind, or link target. A child gets those
facts only when one of its attributes or its own `content` is requested.

`count()` must scan to end because the VArray protocol asks for a count. As a
result, normal Lambda iteration, which obtains a sequence length before its
loop, discovers all direct entry names first. It still does not construct all
child Velmts, stat children, or load their contents. Direct indexed access
remains incremental.

The cached VArray is a **progressive content snapshot**: a discovered name is
stable for the life of the node and later reads reuse it. A portable,
point-in-time atomic directory-membership snapshot would require eagerly
copying every name or an OS-specific snapshot facility, which contradicts the
lazy-VArray requirement. Therefore the final membership is the names observed
while the first scan progresses. If the directory changes during that scan,
the platform's directory-iteration behavior applies; a later
`fs.refresh(node)^` is the explicit way to start a new view.

### 4.2 Ordinary file content

At first `content(file)^`, the backend reads the source bytes once through the
central I/O door of **D7.5.2**, retains that byte snapshot, and invokes the
existing input parser against those bytes with the file's effective source
location/type. The parsed root is then retained as the file's content
snapshot.

The file Velmt has exactly one child for any non-empty successful parsed
input, preserving the input root's type and identity:

```text
<fs ...; "plain text">
<fs ...; {enabled: true}>
<fs ...; <html ...>>
<fs ...; b"bytes">
<fs ...; []>                 // non-empty source whose parsed root is []
<fs ...>                     // empty file: zero children
```

The zero-child empty-file case follows **S2.2.4**. A parsed `null` or empty
array is still one child because it is a successful parsed root, not an empty
source file.

The retained parsed `Input` owner and every cached child `Item` must be traced
by the Velmt state. Raw pointers into an Input arena are not sufficient under
the precise-rooting/GC contract in **D5.2.1v3**.

## 5. ZIP archive content

An ordinary `.zip` archive is physically a file but has directory-like
content. Its first `content(archive)^`:

1. reads and validates the ZIP end record and central directory;
2. normalizes each entry path and synthesizes missing intermediate directory
   nodes;
3. builds a compact archive entry index, not a tree of eager child Velmts;
4. returns a VArray for the archive root's direct entries.

The VArray behavior mirrors a directory: selecting a child creates a Velmt
with its name and archive-entry locator only. Attribute access on that child
uses central-directory metadata lazily; reading its content is deferred until
`content(child)^`.

For an archive-member file, content reads its compressed byte range from the
retained archive snapshot, applies ZIP method 8 raw DEFLATE through zlib, CRC
checks the output, and sends the uncompressed bytes to the ordinary file parse
path. Method 0 (Stored) may be accepted as the no-inflation counterpart; the
required initial compressed format is standard ZIP DEFLATE, not a standalone
zlib or gzip stream.

Initial archive limits and rejections are intentional:

| Accepted initially | Rejected initially |
|---|---|
| Single-disk ZIP32, normalized relative names, DEFLATE (and optionally Stored) entries | Encryption, ZIP64, multi-disk archives, absolute paths, `..` traversal, duplicate normalized names, unsupported compression, and quota violations |

The central directory must be fully indexed to navigate an archive safely; it
is the ZIP-specific exception to an OS directory's incremental name scan. No
entry body is inflated merely by indexing the archive.

## 6. Errors, refresh, and read-only behavior

Both snapshot levels cache their first terminal result. A successful metadata
snapshot, content snapshot, or error is reused by the same node. `fs.refresh`
is the escape hatch rather than a hidden retry.

`fs.refresh(node)^` performs a fresh metadata resolution and returns a new
node with that metadata snapshot already populated and no content snapshot.
It can therefore report a missing path, permission failure, dangling link, or
link cycle immediately, while preserving lazy directory/file/archive content
loading on the new node.

Filesystem Velmts and their VArrays are read-only. They implement no VMap
set/remove or VArray set/splice hooks. A generic virtual operation must carry
`VIRTUAL_OP_ERROR` through to Lambda's ordinary error mechanism; it must never
turn an I/O failure into `null`, a missing child, or an empty sequence.

The current VArray `count` callback has no error status. Before this backend
can ship, the virtual-array protocol needs an error-bearing count path (or an
equivalent checked sequence-boundary operation). Encoding failure as `-1` or
silently treating it as zero would violate the error contract and make a
directory access failure appear to be an empty directory. This is a required
**D7.4.5v2** follow-up, not a backend-local workaround.

## 7. Current implementation versus proposed behavior

Today's Path resolver has two separate caches but eagerly materializes a
directory at content resolution:

| Current path behavior | Proposed FS Velmt behavior |
|---|---|
| First metadata property calls `path_load_metadata()` and permanently stores `PathMeta`. | First attribute request records the independent metadata snapshot. |
| `file_stat()` follows a link via `stat`, then separately identifies a non-dangling link with `lstat`. | `lstat` preserves link identity first; target resolution is explicit and dangling links/cycles error. |
| First iteration/index/length calls `path_resolve_for_iteration()` and stores `path->result`. | First `content(node)^` records the independent content snapshot. |
| Directory resolution calls `dir_list`, creates every child `Path`, and stats every child. | Directory content is a lazy VArray; discovery creates/returns name-seeded child Velmts and does not stat them. |
| File resolution calls eager `input()` and caches the parsed root. | File content snapshots bytes, parses once, and returns a one-child VArray (zero children only for empty source). |
| `input(dir)` separately creates an eager child-Path list. | Preserved unchanged. |

The proposed backend intentionally improves three observable points only on
the new `fs` surface: lazy directory child discovery, meaningful directory
`size: null`, and correct dangling-link identity/error handling. Existing
Path and `input` behavior stay compatible until separately ratified.

## 8. Implementation boundaries

1. Add a core-owned filesystem Velmt factory and state object; do not expose
   virtual headers for modules to initialize directly.
2. Reuse/extend the central `lib/file.*` and input dispatcher behind the one
   I/O gate required by **D7.5.2**. ZIP parsing/decompression belongs on the
   Lambda side of that gate, not in a Node-only binding or vendored source.
3. Implement precise `trace`, `destroy`, and snapshot-version callbacks. The
   tracer roots cached parsed results, archive buffers/indexes, child locator
   state, and any retained `Input` owner.
4. Extend `content`'s element-family dispatch to accept `LMD_TYPE_VELMT` and
   return the backend's VArray child face. Native Element content remains its
   current read-only array view.
5. Fix the generic virtual access helpers so `VIRTUAL_OP_ERROR` is preserved;
   an error output must not be discarded and converted to `ItemNull`.
6. Add focused tests for metadata/content independence, late child metadata,
   directory count versus indexed access, refresh, changing symlinks,
   dangling/cyclic links, empty/parsed-null files, ZIP hierarchy, DEFLATE,
   CRC failures, traversal rejection, quotas, GC stress, and MIR/T0 parity.

No formal-spec amendment is made by this proposal. Before implementation, the
accepted surface, error/effect typing for `content(FsNode)`, and the
status-bearing VArray count change must be ratified in the applicable formal
`S#`/`D#` rulings and reflected back here.

## 9. Rejected shortcuts

- **Make `input()` return an FS Velmt.** Rejected: it changes the existing
  eager parsed-value and eager-directory-list contract of **S12.4.1** and
  **S14.3.1**.
- **Expose ZIP through a new `zip://` Path scheme.** Deferred: archive-member
  addressing is not needed to provide the tree and would alter the Path
  surface prematurely.
- **Use `stat().st_size` as directory size.** Rejected: it is raw,
  platform-specific directory-entry storage, not meaningful recursive data.
- **Build an eager List then call it a VArray.** Rejected by **D7.4.5v2** and
  by the lazy directory requirement.
- **Follow symlinks only during content access.** Rejected: requested link
  attributes must resolve automatically and independently of content.
- **Hide failures as no children.** Rejected: absent, empty, and failed I/O
  are distinct states.
