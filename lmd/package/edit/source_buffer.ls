// source_buffer.ls — the source surface's text buffer
// (vibe/radiant/Radiant_Design_Source_Editor.md §5, CED12).
//
// A buffer is an immutable value: a list of chunks of lines plus the index
// of each chunk's first line. An edit rebuilds only the chunks it touches and
// the index; every other chunk is shared with the previous buffer, so keeping
// old buffers (history, the saved checkpoint) costs nothing extra.
//
// Positions are {line, col}: 0-based line, column in code points (CED21).
// Lines carry no terminator; `eol` and `final_newline` restore the file.

pub let CHUNK = 256

// ---------------------------------------------------------------------------
// Construction and export
// ---------------------------------------------------------------------------

fn strip_cr(l) => if (ends_with(l, "\r")) slice(l, 0, len(l) - 1) else l

// "\r\n" only when every line break is one; a file mixing both keeps its
// carriage returns inside the lines, so writing it back is exact.
fn eol_of(raw) =>
  if (len(raw) > 1 and all([for (l in take(raw, len(raw) - 1)) ends_with(l, "\r")])) "\r\n" else "\n"

// Even split of `lines` into chunks of at most CHUNK lines; never empty.
fn chunks_of(lines) {
  let n = len(lines)
  let k = max(1, (n + CHUNK - 1) div CHUNK)
  let size = max(1, (n + k - 1) div k);
  [for (j in 0 to k - 1) slice(lines, j * size, min((j + 1) * size, n))]
}

// First-line indexes of pieces from chunks_of: all but the last share one size.
fn piece_starts(base, pieces) => [for (j in 0 to len(pieces) - 1) base + j * len(pieces[0])]

fn make(lines, eol, final_newline, version) {
  let chunks = chunks_of(lines)
  {chunks: chunks, starts: piece_starts(0, chunks), count: len(lines), eol: eol,
   final_newline: final_newline, version: version}
}

pub fn from_text(text: string) {
  let raw = split(text, "\n")
  let eol = eol_of(raw)
  let final_newline = len(raw) > 1 and raw[len(raw) - 1] == ""
  let body = if (final_newline) take(raw, len(raw) - 1) else raw
  make(if (eol == "\r\n") [for (l in body) strip_cr(l)] else body, eol, final_newline, 0)
}

pub fn to_text(b) string =>
  join([for (c in b.chunks) join(c, b.eol)], b.eol) ++ (if (b.final_newline) b.eol else "")

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

// The chunk holding line `i`: the last chunk whose first line is <= i.
fn chunk_search(starts, i, lo, hi) {
  if (lo >= hi) lo - 1
  else {
    let mid = (lo + hi) div 2
    if (starts[mid] <= i) chunk_search(starts, i, mid + 1, hi) else chunk_search(starts, i, lo, mid)
  }
}

pub fn chunk_of(b, i) => chunk_search(b.starts, i, 0, len(b.starts))

pub fn line(b, i) string {
  let c = chunk_of(b, i)
  b.chunks[c][i - b.starts[c]]
}

pub fn line_len(b, i) int => len(line(b, i))

// Lines [first, first + n), clipped to the buffer.
pub fn lines(b, first, n) {
  let end_line = min(first + n, b.count);
  [for (i in first to end_line - 1) line(b, i)]
}

// ---------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------

pub fn loc(l, c) => {line: l, col: c}

pub fn pos_cmp(a, b) int =>
  if (a.line != b.line) (if (a.line < b.line) -1 else 1)
  else if (a.col != b.col) (if (a.col < b.col) -1 else 1)
  else 0

pub fn pos_min(a, b) => if (pos_cmp(a, b) <= 0) a else b
pub fn pos_max(a, b) => if (pos_cmp(a, b) >= 0) a else b

pub fn clamp(b, p) {
  let l = min(max(p.line, 0), b.count - 1)
  loc(l, min(max(p.col, 0), line_len(b, l)))
}

pub fn doc_end(b) {
  let l = b.count - 1
  loc(l, line_len(b, l))
}

// The text between two positions, lines joined with "\n".
pub fn text_between(b, from, to) string {
  let a = pos_min(from, to)
  let z = pos_max(from, to)
  if (a.line == z.line) slice(line(b, a.line), a.col, z.col)
  else join([slice(line(b, a.line), a.col, line_len(b, a.line)),
             *lines(b, a.line + 1, z.line - a.line - 1),
             slice(line(b, z.line), 0, z.col)], "\n")
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

// A delta replaces the text between `from` and `to` (from <= to) with
// `insert`, a non-empty list of lines ([""] deletes).
pub fn delta(from, to, insert) => {from: from, to: to, insert: insert}

pub fn text_lines(text: string) => split(text, "\n")

// Where an inserted run of lines ends when it starts at `from`.
pub fn insert_end(from, insert) {
  let n = len(insert)
  if (n == 1) loc(from.line, from.col + len(insert[0]))
  else loc(from.line + n - 1, len(insert[n - 1]))
}

// The replacement lines for one delta, with the kept head and tail joined on.
fn spliced_lines(b, d) {
  let head = slice(line(b, d.from.line), 0, d.from.col)
  let last_text = line(b, d.to.line)
  let tail = slice(last_text, d.to.col, len(last_text))
  let n = len(d.insert)
  if (n == 1) [head ++ d.insert[0] ++ tail]
  else [head ++ d.insert[0], *slice(d.insert, 1, n - 1), d.insert[n - 1] ++ tail]
}

// Apply a delta. Returns the new buffer, the inverse delta (undo), and which
// chunks were rebuilt: `chunk` is the first, `removed` old chunks became
// `added` new ones. The restart-state cache mirrors that (CED16v2).
pub fn apply_delta(b, d) {
  let removed_text = text_lines(text_between(b, d.from, d.to))
  let new_lines = spliced_lines(b, d)
  let cf = chunk_of(b, d.from.line)
  let cl0 = chunk_of(b, d.to.line)
  let base = b.starts[cf]
  let old_region = [for (j in cf to cl0) for (l in b.chunks[j]) l]
  let region = [*slice(old_region, 0, d.from.line - base), *new_lines,
                *slice(old_region, d.to.line - base + 1, len(old_region))]
  // a region shrunk below a quarter chunk merges with the next chunk
  let merge_next = len(region) < CHUNK div 4 and cl0 + 1 < len(b.chunks)
  let cl = if (merge_next) cl0 + 1 else cl0
  let full = if (merge_next) [*region, *b.chunks[cl]] else region
  let pieces = if (len(full) == 0) [[""]] else chunks_of(full)
  let shift = len(new_lines) - (d.to.line - d.from.line + 1)
  let chunks = [*take(b.chunks, cf), *pieces, *drop(b.chunks, cl + 1)]
  let starts = [*take(b.starts, cf), *piece_starts(base, pieces),
                *[for (s in drop(b.starts, cl + 1)) s + shift]]
  {buf: {*: b, chunks: chunks, starts: starts, count: b.count + shift, version: b.version + 1},
   inverse: delta(d.from, insert_end(d.from, d.insert), removed_text),
   chunk: cf, removed: cl - cf + 1, added: len(pieces)}
}
