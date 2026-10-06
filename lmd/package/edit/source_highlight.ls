// source_highlight.ls — syntax highlighting for the source surface
// (vibe/radiant/Radiant_Design_Source_Editor.md CED14v2, CED15, CED16v2).
//
// The Markdown parser reports where each construct lies in a window of the
// buffer (parse with sourcepos: 'spans'); this module turns those spans into
// per-line color runs. Classes are the only policy here: the parser decides
// what a construct is, so the highlighter never disagrees with it.
//
// A run is {s, e, c}: code-point columns [s, e) and a CSS class. A line's runs
// are sorted, disjoint, and leave plain text uncovered.

import buf: lambda.edit.source_buffer

// The language a file highlights as, or null for plain text.
pub fn language_of(path) {
  let p = lower(path)
  if (ends_with(p, ".md") or ends_with(p, ".markdown")) 'markdown' else null
}

// ---------------------------------------------------------------------------
// Runs
// ---------------------------------------------------------------------------

fn run(s, e, c) => {s: s, e: e, c: c}

// Paint [s, e) with class c over existing runs: later paint wins, so nested
// constructs are painted after the constructs that contain them.
fn paint(runs, s, e, c) {
  if (e <= s) runs
  else {
    let kept = [for (r in runs) for (part in [run(r.s, min(r.e, s), r.c), run(max(r.s, e), r.e, r.c)]
                  where part.e > part.s and (part.e <= s or part.s >= e)) part];
    sort([*kept, run(s, e, c)], (r) => r.s)
  }
}

// ---------------------------------------------------------------------------
// Classes: the only highlighting policy
// ---------------------------------------------------------------------------

fn block_class(kind) {
  if (kind == 'h1' or kind == 'h2' or kind == 'h3' or kind == 'h4' or kind == 'h5' or kind == 'h6') "tok-heading"
  else if (kind == 'code' or kind == 'pre') "tok-code"
  else if (kind == 'hr') "tok-hr"
  else if (kind == 'math') "tok-math"
  else if (kind == 'html-block') "tok-html"
  else null
}

fn inline_class(kind) {
  if (kind == 'em' or kind == 'i') "tok-emphasis"
  else if (kind == 'strong' or kind == 'b') "tok-strong"
  else if (kind == 'del' or kind == 's') "tok-strike"
  else if (kind == 'code') "tok-code"
  else if (kind == 'a' or kind == 'img') "tok-link"
  else if (kind == 'math') "tok-math"
  else if (kind == 'raw-html') "tok-html"
  else "tok-markup"
}

// ---------------------------------------------------------------------------
// Line-level marks the walker reads from the text: container markers (the
// parser reports a list or quote as one block), fence lines, table pipes.
// ---------------------------------------------------------------------------

fn indent_len(t) => len(t) - len(trim_start(t))

fn is_digit(ch) => ch >= "0" and ch <= "9"

// The width of a list marker at column i ("- ", "12. ", "3) "), or 0.
fn bullet_width(t, i) {
  let ch = slice(t, i, i + 1)
  if ((ch == "-" or ch == "*" or ch == "+") and (slice(t, i + 1, i + 2) == " " or i + 1 == len(t))) 1
  else {
    let digits = len([for (j in i to min(len(t), i + 9) - 1 where is_digit(slice(t, j, j + 1))) j])
    let after = slice(t, i + digits, i + digits + 1)
    if (digits > 0 and digits == len(trim_start(slice(t, i, i + digits))) and (after == "." or after == ")")) digits + 1
    else 0
  }
}

fn list_marks(t) {
  let i = indent_len(t)
  let w = bullet_width(t, i)
  let task = slice(t, i + w + 1, i + w + 4)
  let box = if (w > 0 and (task == "[ ]" or task == "[x]" or task == "[X]")) 4 else 0;
  if (w == 0) [] else [run(i, i + w + box, "tok-list-marker")]
}

// Leading `>` markers, each with the space after it.
fn quote_end(t, i) {
  let j = i + indent_len(slice(t, i, len(t)))
  if (slice(t, j, j + 1) == ">") quote_end(t, j + 1) else i
}

fn quote_marks(t) {
  let e = quote_end(t, 0);
  if (e == 0) [] else [run(indent_len(t), e, "tok-quote")]
}

fn is_fence_line(t) {
  let s = trim_start(t)
  starts_with(s, "```") or starts_with(s, "~~~")
}

fn table_marks(t) {
  let delimiter_row = len(trim(t)) > 0 and
                      len([for (ch in t where ch != "|" and ch != "-" and ch != ":" and ch != " ") ch]) == 0;
  if (delimiter_row) [run(0, len(t), "tok-table-delim")]
  else [for (i in 0 to len(t) - 1 where slice(t, i, i + 1) == "|") run(i, i + 1, "tok-table-delim")]
}

// A link's destination: from "](" to its end, or the whole of an autolink.
fn link_url_start(t, s, e) {
  let at = index_of(slice(t, s, e), "](")
  if (slice(t, s, s + 1) == "<") s
  else if (at == null) e
  else s + at + 1
}

fn link_def_marks(t) {
  let colon = index_of(t, "]:");
  if (colon == null) [] else [run(0, colon + 2, "tok-ref"), run(colon + 2, len(t), "tok-url")]
}

// YAML front matter: the lines from a first-line `---` to its closing line.
pub fn front_matter_end(b) {
  if (b.count < 2 or trim_end(buf.line(b, 0)) != "---") -1
  else {
    let limit = min(b.count, 400)
    let closes = [for (i in 1 to limit - 1 where trim_end(buf.line(b, i)) == "---" or trim_end(buf.line(b, i)) == "...") i];
    if (len(closes) == 0) -1 else closes[0]
  }
}

// ---------------------------------------------------------------------------
// Spans to runs
// ---------------------------------------------------------------------------

let FIELDS = 6

fn span_at(r, i) {
  let f = r[1];
  {kind: r[0][f[i * FIELDS]], block: f[i * FIELDS + 1] == 1, line: f[i * FIELDS + 2], col: f[i * FIELDS + 3],
   end_line: f[i * FIELDS + 4], end_col: f[i * FIELDS + 5]}
}

// The [s, e) a span covers on `line`, clipped to the line's text.
fn span_cols(sp, line, text) =>
  {s: if (line == sp.line) sp.col else 0, e: if (line == sp.end_line) min(sp.end_col, len(text)) else len(text)}

// Runs for one line from the spans touching it: blocks first, then inline
// constructs outermost first, so nested constructs paint over their parents.
fn line_runs(text, line, spans, fm_end) {
  if (line <= fm_end) [run(0, len(text), "tok-meta")]
  else {
    let here = [for (sp in spans where sp.line <= line and sp.end_line >= line) sp]
    let blocks = [for (sp in here where sp.block) sp]
    let inlines = sort([for (sp in here where not sp.block) sp],
                       (sp) => 0 - ((sp.end_line - sp.line) * 100000 + sp.end_col - sp.col))
    let base = block_runs(text, line, blocks)
    inline_runs(text, line, inlines, 0, base)
  }
}

fn block_runs(text, line, blocks) =>
  if (len(blocks) == 0) []
  else {
    let sp = blocks[0]
    let k = sp.kind;
    if (k == 'ul' or k == 'ol') list_marks(text)
    else if (k == 'blockquote') quote_marks(text)
    else if (k == 'table') table_marks(text)
    else if (k == 'link_def') link_def_marks(text)
    else if (k == 'code' and is_fence_line(text) and (line == sp.line or line == sp.end_line)) [run(0, len(text), "tok-fence")]
    else if (block_class(k) == null or len(text) == 0) []
    else [run(0, len(text), block_class(k))]
  }

fn inline_runs(text, line, spans, i, runs) {
  if (i >= len(spans)) runs
  else {
    let sp = spans[i]
    let cols = span_cols(sp, line, text)
    let c = inline_class(sp.kind)
    let painted = paint(runs, cols.s, cols.e, c)
    let with_url = if ((sp.kind == 'a' or sp.kind == 'img') and line == sp.line and line == sp.end_line)
                     paint(painted, link_url_start(text, cols.s, cols.e), cols.e, "tok-url")
                   else painted
    inline_runs(text, line, spans, i + 1, with_url)
  }
}

// Parse the window [first, last] and build its runs. `scan` is the restart
// cache {states, valid} the previous call returned.
pub fn highlight(b, first, final, scan) {
  let lo = max(0, first)
  let hi = min(b.count - 1, final)
  let r = parse(b.chunks, {type: 'markdown', sourcepos: 'spans', window: [lo, hi], prescan: scan}) or null
  if (r == null) null
  else {
    let spans = [for (i in 0 to len(r[1]) div FIELDS - 1) span_at(r, i)]
    let fm_end = front_matter_end(b);
    {hl: {version: b.version, first: lo, last: hi, exact: true,
          runs: [for (l in lo to hi) line_runs(buf.line(b, l), l, spans, fm_end)]},
     scan: {states: r[2], valid: len(r[2]) div 3}}
  }
}

// The runs for `line`, or null when the line is not highlighted (plain).
pub fn runs_for(hl, b, line) =>
  if (hl == null or hl.version != b.version or line < hl.first or line > hl.last) null
  else hl.runs[line - hl.first]

// Whether `hl` already covers [first, last] exactly for this buffer.
pub fn covers(hl, b, first, final) =>
  hl != null and hl.exact and hl.version == b.version and hl.first <= max(0, first) and
  hl.last >= min(b.count - 1, final)

// ---------------------------------------------------------------------------
// Provisional runs (CED14v2): an edit moves the colors with the text until the
// next frame swaps in exact runs.
// ---------------------------------------------------------------------------

fn clip(runs, s, e) => [for (r in runs where r.e > s and r.s < e) run(max(r.s, s), min(r.e, e), r.c)]
fn shift(runs, d) => [for (r in runs) run(r.s + d, r.e + d, r.c)]

// `n` inserted columns at `col` join the run ending there (the token being
// typed); text inserted elsewhere is plain until the swap.
fn grow_left(head, col, n) =>
  [for (r in head) if (r.e == col and r.s < col) run(r.s, r.e + n, r.c) else r]

fn merge(runs) {
  if (len(runs) < 2) runs
  else {
    let rest = merge(drop(runs, 1))
    let a = runs[0];
    if (len(rest) > 0 and rest[0].s == a.e and rest[0].c == a.c) [run(a.s, rest[0].e, a.c), *drop(rest, 1)]
    else [a, *rest]
  }
}

// The runs of the lines one delta replaces, from the old first and last lines.
fn edited_lines(first_runs, last_runs, d) {
  let n = len(d.insert)
  let head = clip(first_runs, 0, d.from.col)
  let tail = clip(last_runs, d.to.col, 1000000000)
  let width_first = len(d.insert[0])
  let width_last = len(d.insert[n - 1]);
  if (n == 1) [merge([*grow_left(head, d.from.col, width_first), *shift(tail, d.from.col + width_first - d.to.col)])]
  else [grow_left(head, d.from.col, width_first),
        *[for (i in 1 to n - 2) []],
        shift(tail, width_last - d.to.col)]
}

// Map runs through one delta. An edit that reaches outside the window drops
// the runs: rows show plain until the swap.
pub fn map_runs(hl, d, version) {
  if (hl == null) null
  else if (d.from.line < hl.first or d.to.line > hl.last) null
  else {
    let k0 = d.from.line - hl.first
    let k1 = d.to.line - hl.first
    let lines = edited_lines(hl.runs[k0], hl.runs[k1], d)
    let runs = [*take(hl.runs, k0), *lines, *drop(hl.runs, k1 + 1)];
    {*: hl, version: version, exact: false, runs: runs, last: hl.first + len(runs) - 1}
  }
}

// The restart cache after an edit that rebuilt chunks [chunk, chunk + removed)
// as `added` chunks: boundaries up to the edited chunk stay valid; later ones
// are suspect but kept, realigned, for the next parse to revalidate.
pub fn scan_after(scan, applied) {
  let states = scan.states
  let keep = min(len(states), (applied.chunk + 1) * 3)
  let after = min(len(states), (applied.chunk + applied.removed) * 3)
  let fresh = [for (i in 1 to (applied.added - 1) * 3) 0];
  {states: [*take(states, keep), *fresh, *drop(states, after)], valid: min(scan.valid, applied.chunk + 1)}
}

pub fn empty_scan() => {states: [], valid: 0}

// Apply the CED14v2 mapping for a sequence of applied edits: each step is
// {delta, applied}, where applied is source_buffer.apply_delta's result.
pub fn after_steps(hl, scan, steps, version) {
  if (steps == null) {hl: null, scan: empty_scan()}
  else map_steps(hl, scan, steps, 0, version)
}

fn map_steps(hl, scan, steps, i, version) {
  if (i >= len(steps)) {hl: hl, scan: scan}
  else {
    let st = steps[i]
    map_steps(map_runs(hl, st.delta, version), scan_after(scan, st.applied), steps, i + 1, version)
  }
}

pub let css = "
  .tok-heading { color: #0550ae; font-weight: normal; }
  .tok-strong { color: #953800; }
  .tok-emphasis { color: #8250df; font-style: italic; }
  .tok-strike { color: #6e7781; text-decoration: line-through; }
  .tok-code, .tok-fence { color: #0a3069; background: #f6f8fa; }
  .tok-fence { color: #6e7781; }
  .tok-link { color: #0969da; }
  .tok-url { color: #6e7781; text-decoration: underline; }
  .tok-ref { color: #0969da; }
  .tok-math { color: #116329; }
  .tok-html { color: #116329; }
  .tok-hr, .tok-list-marker, .tok-quote, .tok-table-delim { color: #cf222e; }
  .tok-meta { color: #6e7781; }
  .tok-markup { color: #6639ba; }
"
