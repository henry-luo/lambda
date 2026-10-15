# Git and patch review

`lambda.doc.diff` parses saved patches and reviews local Git changes. The package
owns parsing, comparison policy and presentation (D7.2.4/D7.2.6); Radiant supplies
DOM, layout, selection and clipboard transport (D7.5.3). Repository operations
are procedures; viewer construction and patch parsing are pure (S12.1.1v3/S12.1.3).

Create `review.ls` and open it with `./lambda.exe view review.ls`:

```lambda no-run
import diff: lambda.doc.diff
diff.repository(".", {mode: "unstaged"})^
```

Saved patches open directly with `./lambda.exe view change.patch` or
`./lambda.exe view change.diff`. `layout` and `render` use the complete static
projection. Git is required for repository operations; saved patch parsing and
presentation work without it.

## Public API

| Export | Contract |
|---|---|
| `parse(source, options = null)` | Pure; returns a checked diff document from text or binary. `null` represents empty bytes (S2.2.2v2). |
| `read(path, options = null)` | Procedure; reads a patch as bytes, then parses it. |
| `to_html(document, options = null)` | Pure; returns a complete HTML element tree for every file in a loaded document. |
| `viewer(document, options = null)` | Pure; constructs an interactive HTML document over captured data. |
| `repository(path, options = null)` | Pure; constructs a viewer whose mount handler discovers and reads the repository. |
| `open_repo(path, options = null)` | Procedure; discovers an absolute worktree root, Git directory and HEAD tree. Supports linked worktrees and nested launch directories. |
| `compare(repo, comparison = "unstaged", options = null)` | Procedure; captures a file manifest and comparison provenance. |
| `load_file(repo, manifest, id, options = null)` | Procedure; returns a loaded, single-file diff document for a manifest file ID. |

All exports report failures through `^` (S7.4.2/S7.6). Package validation errors
use code 7810; underlying process, input and grammar errors keep their codes.
Unknown options are rejected.

### Comparisons and options

`comparison` is a mode name or a map:

| Mode | Compares |
|---|---|
| `"unstaged"` | Index against worktree; also lists untracked files separately. |
| `"staged"` | HEAD against index; supports an unborn HEAD. |
| `"working"` | HEAD against worktree; also lists untracked files. Requires HEAD. |
| `{mode: "commits", base: "HEAD~1", target: "HEAD"}` | Two revisions, resolved to immutable tree IDs before capture. |

Git operations accept `timeout_ms` (default 30000, zero disables the deadline)
and `max_output_bytes` (default 16777216, positive, for each captured stream).
They use argument arrays and binary paths, with literal pathspecs. Color,
external diff, textconv and configured clean/smudge/process filters are disabled;
Git commands acquire no network resources and perform no repository writes.

`parse`/`read` accept `profile` (`"auto"`, `"git"`, `"unified"`), `max_bytes`
(default 16777216) and `max_records` (default 200000). `max_records` is checked
after the grammar produces its bounded source's record tree.

`viewer` accepts `title`, `rows` (default 28) and `file_rows` (default 18).
`repository` accepts these plus `mode`, `base`, `target`, `timeout_ms` and
`max_output_bytes`. `to_html` accepts `title`. A loaded document is required for
static output; a manifest has no file bodies to export.

```lambda no-run
import diff: lambda.doc.diff

pn main() {
    let repo = diff.open_repo(".")^
    let manifest = diff.compare(repo, {mode: "commits", base: "HEAD~1", target: "HEAD"})^
    if (len(manifest.files) > 0) {
        let document = diff.load_file(repo, manifest, manifest.files[0].id)^
        output(format(diff.to_html(document)^, "html")^, "temp/review.html", "text")^
    }
}
```

## Parsing and model

The syntax is declared in `lambda.doc.diff.grammar`, using the Lambda grammar
engine (GM1–GM4/GM10/GM11v2/GM14). A record grammar recognizes headers, metadata,
hunk ranges, payload prefixes and newline markers. Pure lowering then consumes
the declared old/new line counts, validates ranges and groups files/hunks.
This distinguishes payload such as `--- a/literal` from a conventional unified
file header without a second handwritten lexer.

The document retains its original binary `source`. Each file has a sequential
`id`, byte/display paths, kind, modes, binary/submodule flags, hunks and counts.
Each hunk has `old_start`, `old_count`, `new_start`, `new_count`, `header` and
`lines`. Each line has `kind` (`"add"`, `"remove"`, `"context"`), nullable
`old_line`/`new_line`, half-open payload byte spans `start`/`end`, and old/new
missing-newline flags. Spans omit the prefix and LF; a source CR is preserved.
Absent sides are `null`; zero is never used as a fake line number.

Quoted Git paths are decoded into bytes. NUL-delimited Git metadata also uses
the grammar engine. Invalid UTF-8 is preserved in the source and path identity;
its display uses a binary literal. UI columns are codepoint positions (S2.5.8),
converted at the DOM UTF-16 boundary. Display/copy never changes the snapshot.

Supported inputs include Git two-way patches, conventional unified patches,
add/delete, rename/copy metadata, mode-only and binary summaries, symlinks and
submodule pointer changes. Truncated hunks, extra payload, invalid newline
markers and overlapping ranges are rejected. Combined merge patches are
explicitly unsupported. Conflict paths in a repository show a conflict summary.

Live index/worktree comparisons are observations, not atomic filesystem
snapshots. Loading checks the captured raw manifest entry and compares two patch
captures. A detected change asks the user to refresh; zero worktree object IDs
do not establish that content remained unchanged since manifest capture.

## Review controls

The changed-file list and unified rows are virtual. Selecting a repository file
loads only that body; refresh and mode/file changes cancel superseded tasks and
check request generations before publishing results. Refresh preserves the
selected path and scroll position when possible. Search covers the loaded file,
including rows outside the viewport. Previous/next hunk, page controls, wheel
scrolling and keyboard navigation move the viewport.

Copy old/new copies that side's visible patch content, including offscreen
hunks, without line numbers or hunk headers. It cannot recover unchanged text
outside the patch's context. Ctrl/Cmd+A selects the model's entire unified file;
Ctrl/Cmd+C copies the selection across virtual pages. Shift+navigation extends
selection; a line-number press selects the line. Shift-click extends from the
model anchor even when that anchor is offscreen. Missing final newlines are
preserved. The platform clipboard accepts text; invalid-byte displays copy
their explicit binary-literal representation.

Only the current loaded body is retained in view state. Completed task handles
retain their results while referenced; releasing a handle permits its graph and
mailbox to be collected (D5.3.3). Small native task records, including scalar and
fault homes, remain until the document scheduler closes.

Side-by-side layout, intraline highlights, context expansion, automatic refresh
and Git write operations are outside this initial unified viewer.

See the [design proposal](../vibe/Lambda_Pkg_Diff.md) and
[implementation record](../vibe/impl/Lambda_Impl_Diff.md).
