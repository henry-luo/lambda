// source.ls — the source-code editing surface of `lambda edit`
// (vibe/radiant/Radiant_Design_Source_Editor.md, CED11–CED22).
//
// The document is a source_buffer value (CED12). Only a window of lines is
// ever rendered (CED13): the window is projected as a lambda.editor-shaped
// document — one node per line, one text leaf per line — and applied, so the
// render map records each line's source path and the native selection bridge
// maps DOM positions to [row, 0] plus a UTF-8 byte offset, and back again
// after every render (CED19). Model selection is truth; the DOM selection is
// its projection, clamped to the window (CED4).

import dom
import edit_result: lambda.dom.edit_result
import sess: lambda.edit.session
import tools: lambda.edit.toolbar
import files: lambda.edit.files
import buf: lambda.edit.source_buffer
import syn: lambda.edit.source_highlight
import units: lambda.edit.source_units
import lambda.editor.mod_doc
import lambda.editor.mod_source_pos
import lambda.editor.mod_editor
// rich_text owns the package's `view map` template, which projects
// editor-shaped documents and records their source paths
import rich: lambda.edit.rich_text

// ---------------------------------------------------------------------------
// Format descriptor
// ---------------------------------------------------------------------------

let suffixes = [".txt", ".text", ".log", ".ls", ".json", ".jsonl", ".yaml", ".yml", ".toml", ".ini",
                ".conf", ".cfg", ".csv", ".tsv", ".xml", ".css", ".js", ".mjs", ".ts", ".tex",
                ".c", ".h", ".cpp", ".hpp", ".py", ".rb", ".sh", ".lua", ".sql", ".rst", ".org"]

fn import_text(text) => {doc: buf.from_text(text), envelope: null}
fn export_text(doc, envelope) => buf.to_text(doc)
// the buffer keeps the file's bytes, so a save always writes back exactly
fn check_roundtrip(doc, envelope) => null
// versions only grow, so comparing them is O(1) (design §5.1)
fn same_doc(a, b) => a.version == b.version

pub let descriptor = {
  id: 'source', name: "source text", suffixes: suffixes, surface: 'source',
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  same_doc: same_doc
}

// ---------------------------------------------------------------------------
// Geometry. The view `vw` is the surface's scroll position and measurements:
// `top` is the first rendered line and `row` how many of its wrapped rows lie
// above the surface (design §6.4); `rows` is the rendered row count (the
// viewport's rows plus OVERSCAN) and `cols` the text width in characters.
// Without `wrap` every line is one row and `row` stays 0, and `left` columns
// are scrolled out of view to the left (design §6.2); `char_w` is the
// measured advance of one column.
// ---------------------------------------------------------------------------

let LINE_H = 20.0
// rows rendered before the viewport is measured
let DEFAULT_ROWS = 48
let OVERSCAN = 2
// the CSS default tab stop, which the surface keeps
let TAB = 8
// a hidden span of this text measures the font's advance once, on mount
let MEASURE_CHARS = 20
let MEASURE_TEXT = "00000000000000000000"
// the text column's horizontal padding (.src-text) and a caret's width
let TEXT_PAD = 14.0

fn new_view() => {handle: null, top: 0, row: 0, rows: DEFAULT_ROWS, cols: 0, wrap: false, left: 0, char_w: 0.0}

// Rendered rows for a surface `height` pixels tall.
fn rows_for(height) => max(1, int(height / LINE_H) + OVERSCAN)

fn visible(vw) => max(1, vw.rows - OVERSCAN)

// Columns a line occupies: a tab advances to the next stop.
fn expanded_len(parts, i, col) =>
  if (i == len(parts) - 1) col + len(parts[i])
  else expanded_len(parts, i + 1, ((col + len(parts[i])) div TAB + 1) * TAB)

fn display_len(text) => if (contains(text, "\t")) expanded_len(split(text, "\t"), 0, 0) else len(text)

fn wrapping(vw) => vw.wrap and vw.cols > 0

// Rows line `l` takes. The surface breaks wrapped lines at any character
// (`word-break: break-all`), so a monospace line's rows are its columns
// divided by the width.
fn line_rows(b, vw, l) =>
  if (not wrapping(vw)) 1 else max(1, (display_len(buf.line(b, l)) + vw.cols - 1) div vw.cols)

// The wrapped row a position lies on within its line.
fn pos_row(b, vw, p) =>
  if (not wrapping(vw)) 0
  else min(display_len(slice(buf.line(b, p.line), 0, p.col)) div vw.cols, line_rows(b, vw, p.line) - 1)

// Lines rendered from the view's top: enough to fill `rows`.
fn fill_count(b, vw, l, filled, n) =>
  if (l >= b.count or filled >= vw.rows) n
  else fill_count(b, vw, l + 1, filled + line_rows(b, vw, l), n + 1)

fn window_count(b, vw) => fill_count(b, vw, vw.top, 0 - vw.row, 0)

// The (line, row) `n` rows after (line, row), or before it for n < 0,
// stopping at the document's ends.
fn row_step(b, vw, line, row, n) {
  if (n == 0) {line: line, row: row}
  else if (n > 0) {
    let below = line_rows(b, vw, line) - row - 1
    if (n <= below) {line: line, row: row + n}
    else if (line + 1 >= b.count) {line: line, row: row + below}
    else row_step(b, vw, line + 1, 0, n - below - 1)
  }
  else {
    if (0 - n <= row) {line: line, row: row + n}
    else if (line == 0) {line: 0, row: 0}
    else row_step(b, vw, line - 1, line_rows(b, vw, line - 1) - 1, n + row + 1)
  }
}

fn row_before(p, q) => p.line < q.line or (p.line == q.line and p.row < q.row)

// Scrolling stops when the document's last row reaches the bottom.
fn clamped(b, vw, p) {
  let l = b.count - 1
  let stop = row_step(b, vw, l, line_rows(b, vw, l) - 1, 1 - visible(vw))
  let q = if (row_before(stop, p)) stop else p
  {*: vw, top: q.line, row: q.row}
}

// The view scrolled by `n` rows (wheel, track), or to a line (thumb).
fn scrolled(b, vw, n) => clamped(b, vw, row_step(b, vw, vw.top, vw.row, n))
fn scrolled_to(b, vw, line) => clamped(b, vw, {line: max(0, min(line, b.count - 1)), row: 0})

// Rows from the view's first row through row `r` of `line`, which lies at or
// after the top.
fn rows_through(b, vw, line, r) =>
  if (line == vw.top) r - vw.row + 1
  else line_rows(b, vw, vw.top) - vw.row + r + 1 +
       (if (line - vw.top < 2) 0 else sum([for (l in vw.top + 1 to line - 1) line_rows(b, vw, l)]))

// The view that keeps position `p` visible: unchanged when it already is,
// else scrolled just enough, as the caret moves in every editor.
fn follow(b, vw, p) => follow_left(b, follow_rows(b, vw, p), p)

fn follow_rows(b, vw, p) {
  let r = pos_row(b, vw, p)
  if (p.line < vw.top or (p.line == vw.top and r < vw.row)) {*: vw, top: p.line, row: r}
  else if (p.line - vw.top < visible(vw) and rows_through(b, vw, p.line, r) <= visible(vw)) vw
  else {
    let q = row_step(b, vw, p.line, r, 1 - visible(vw));
    {*: vw, top: q.line, row: q.row}
  }
}

// The display column (tabs expanded) of code-point column `col` of a line.
fn display_col(text, col) => display_len(slice(text, 0, col))

// The code-point column at display column `d`: a column inside a tab's
// expansion lands before the tab, one past the end at the end.
fn col_at_display(parts, i, d, at, col) {
  let n = len(parts[i])
  if (d <= at + n or i == len(parts) - 1) col + min(max(d - at, 0), n)
  else {
    let stop = ((at + n) div TAB + 1) * TAB
    if (d < stop) col + n else col_at_display(parts, i + 1, d, stop, col + n + 1)
  }
}

fn col_of_display(text, d) =>
  if (contains(text, "\t")) col_at_display(split(text, "\t"), 0, d, 0, 0) else min(max(d, 0), len(text))

// Unwrapped, the text column scrolls sideways to keep `p` in view, with a
// few columns of context; wrapped lines never need it.
fn follow_left(b, vw, p) {
  if (wrapping(vw) or vw.cols <= 0) (if (vw.left == 0) vw else {*: vw, left: 0})
  else {
    let x = display_col(buf.line(b, p.line), p.col)
    let context = min(8, vw.cols div 4)
    if (x < vw.left) {*: vw, left: max(0, x - context)}
    else if (x >= vw.left + vw.cols - 1) {*: vw, left: x - vw.cols + 1 + context}
    else vw
  }
}

// The farthest the text column scrolls: the widest rendered line's end, in view.
fn max_left(b, vw) {
  let widths = [for (l in vw.top to vw.top + window_count(b, vw) - 1) display_len(buf.line(b, l))]
  max(0, (if (len(widths) == 0) 0 else max(widths)) - vw.cols + 2)
}

// The view scrolled sideways by `n` columns.
fn scrolled_left(b, vw, n) =>
  if (wrapping(vw)) vw else {*: vw, left: min(max(0, vw.left + n), max(vw.left, max_left(b, vw)))}

// Wheel motion arrives in pixels; whole lines move the window and the
// remainder carries to the next event, so slow trackpad motion still scrolls.
fn wheel_step(px) {
  let lines = int(px / LINE_H)
  {lines: lines, rest: px - float(lines) * LINE_H}
}

// ---------------------------------------------------------------------------
// Window projection
// ---------------------------------------------------------------------------

// A row's text leaves: highlighted runs and the plain text between them. The
// render and the selection mapping both read rows through this, so a
// leaf index in a source path always means the same column range.
fn leaf(t, c, s) => {text: t, cls: c, start: s}

fn leaves_from(t, runs, i, at, acc) {
  if (i >= len(runs)) {
    if (at < len(t) or len(acc) == 0) [*acc, leaf(slice(t, at, len(t)), null, at)] else acc
  } else {
    let r = runs[i]
    let s = max(r.s, at)
    let e = min(r.e, len(t))
    if (e <= s) leaves_from(t, runs, i + 1, at, acc)
    else {
      let gap = if (s > at) [leaf(slice(t, at, s), null, at)] else [];
      leaves_from(t, runs, i + 1, e, [*acc, *gap, leaf(slice(t, s, e), r.c, s)])
    }
  }
}

// `hs` is the paint state: syntax runs, with find matches laid over them.
fn row_leaves(b, hs, line) {
  let t = buf.line(b, line)
  let runs = syn.overlay(syn.runs_for(hs.hl, b, line) or [], find_marks(t, hs.find), "src-match")
  if (len(runs) == 0) [leaf(t, null, 0)] else leaves_from(t, runs, 0, 0, [])
}

// rich_text's `view map` renders a text leaf with a `cls` as a classed span
fn window_doc(b, vw, hs) =>
  node('doc', [for (l in vw.top to vw.top + window_count(b, vw) - 1)
                 node('src_line', [for (lf in row_leaves(b, hs, l)) {*: text(lf.text), cls: lf.cls}])])

// The rows of the top line above the surface are hidden by shifting the
// text and the gutter up together.
fn shift_style(vw) => if (vw.row > 0) "margin-top: " ++ string(0.0 - float(vw.row) * LINE_H) ++ "px" else null

fn left_px(vw) => float(vw.left) * vw.char_w

// The text column scrolled sideways slides under the gutter, which paints
// above it; a negative margin widens it by as much, so its right edge stays.
fn surface_style(vw) =>
  if (vw.row > 0) shift_style(vw)
  else if (vw.left > 0 and vw.char_w > 0.0) "margin-left: " ++ string(0.0 - left_px(vw)) ++ "px"
  else null

// A wrapped line's number keeps the height of all its rows.
fn line_number(b, vw, l) {
  let n = line_rows(b, vw, l)
  if (n == 1) <div class: "src-num", ["data-line"]: string(l), string(l + 1)>
  else <div class: "src-num", ["data-line"]: string(l), style: "height: " ++ string(float(n) * LINE_H) ++ "px",
            string(l + 1)>
}

fn gutter(b, vw) =>
  <div class: "src-gutter", ["aria-hidden"]: "true", style: shift_style(vw),
    *[for (l in vw.top to vw.top + window_count(b, vw) - 1) line_number(b, vw, l)]
  >

// The scrollbar shows where the window is; a press on the track pages.
fn scrollbar(b, vw) {
  let span = max(1, b.count)
  let top = vw.top
  let shown = min(1.0, float(vw.rows) / float(span));
  <div class: "src-scroll", ["aria-hidden"]: "true",
    <div class: "src-thumb",
      style: "top: " ++ string(100.0 * float(top) / float(span)) ++ "%; height: " ++
             string(max(2.0, 100.0 * shown)) ++ "%">
  >
}

// The leaf holding column `col`: the last one starting at or before it.
fn leaf_index(leaves, col) => max(0, len([for (lf in leaves where lf.start <= col) lf]) - 1)

// A position on a rendered row as a source position: [row, leaf] and a UTF-8
// byte offset into that leaf (CED21).
fn row_pos(b, hs, row, line, col) {
  let leaves = row_leaves(b, hs, line)
  let j = leaf_index(leaves, col)
  pos([row, j], units.bytes_before(leaves[j].text, col - leaves[j].start))
}

// A model position as a source position in the rendered window, clamped to it.
fn to_source(b, vw, hs, p) {
  let top = vw.top
  let n = window_count(b, vw)
  if (n == 0 or p.line < top) pos([0, 0], 0)
  else if (p.line >= top + n) row_pos(b, hs, n - 1, top + n - 1, buf.line_len(b, top + n - 1))
  else row_pos(b, hs, p.line - top, p.line, p.col)
}

fn source_selection_of(b, vw, hs, sel) {
  let anchor = to_source(b, vw, hs, sel.anchor)
  let head = to_source(b, vw, hs, sel.head)
  text_selection(anchor, head)
}

// A source position from the rendered window as a model position.
fn from_source(b, top, hs, sp) {
  let path = sp.path
  if (len(path) == 0) buf.clamp(b, buf.loc(top + sp.offset, 0))
  else {
    let l = min(top + path[0], b.count - 1)
    let leaves = row_leaves(b, hs, l)
    if (len(path) == 1) buf.loc(l, if (sp.offset < len(leaves)) leaves[sp.offset].start else buf.line_len(b, l))
    else if (path[1] >= len(leaves)) buf.loc(l, buf.line_len(b, l))
    else buf.loc(l, leaves[path[1]].start + units.col_at_byte(leaves[path[1]].text, sp.offset))
  }
}

fn model_selection(b, top, hs, ssel) =>
  if (ssel == null or ssel.kind != 'text') null
  else {anchor: from_source(b, top, hs, ssel.anchor), head: from_source(b, top, hs, ssel.head), goal: null}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

fn caret(p) => {anchor: p, head: p, goal: null}
fn collapsed(sel) => buf.pos_cmp(sel.anchor, sel.head) == 0
fn sel_from(sel) => buf.pos_min(sel.anchor, sel.head)
fn sel_to(sel) => buf.pos_max(sel.anchor, sel.head)

// The text a copy or cut puts on the clipboard, with the file's line ends;
// null for an empty selection.
fn clipboard_text(b, sel) {
  if (collapsed(sel)) null
  else {
    let t = buf.text_between(b, sel.anchor, sel.head)
    if (b.eol == "\n") t else replace(t, "\n", b.eol)
  }
}

// The selection an action applies to: the model's own when the DOM still
// shows its projection (it may reach past the window), else the DOM's.
fn action_selection(b, vw, hs, sel, evt) {
  let dom_sel = model_selection(b, vw.top, hs, evt.source_selection)
  if (dom_sel == null) sel
  else if (source_selection_of(b, vw, hs, sel) == evt.source_selection) sel
  else dom_sel
}

// ---------------------------------------------------------------------------
// Cursor motion
// ---------------------------------------------------------------------------

fn is_word_char(ch) => (ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z") or
                       (ch >= "0" and ch <= "9") or ch == "_" or ord(ch) > 127

fn prev_pos(b, p) =>
  if (p.col > 0) buf.loc(p.line, p.col - 1)
  else if (p.line > 0) buf.loc(p.line - 1, buf.line_len(b, p.line - 1))
  else p

fn next_pos(b, p) =>
  if (p.col < buf.line_len(b, p.line)) buf.loc(p.line, p.col + 1)
  else if (p.line + 1 < b.count) buf.loc(p.line + 1, 0)
  else p

// Skip spaces, then one run of word or punctuation characters.
fn word_left_col(text, col) {
  let skip = col - len(trim_end(slice(text, 0, col)))
  let start = col - skip
  if (start == 0) 0
  else {
    let word = is_word_char(slice(text, start - 1, start))
    let run = [for (i in 0 to start - 1 where is_word_char(slice(text, start - 1 - i, start - i)) != word) i]
    if (len(run) == 0) 0 else start - run[0]
  }
}

fn word_right_col(text, col) {
  let n = len(text)
  let rest = slice(text, col, n)
  let start = col + (len(rest) - len(trim_start(rest)))
  if (start >= n) n
  else {
    let word = is_word_char(slice(text, start, start + 1))
    let run = [for (i in start to n - 1 where is_word_char(slice(text, i, i + 1)) != word) i]
    if (len(run) == 0) n else run[0]
  }
}

fn word_left(b, p) => if (p.col == 0) prev_pos(b, p) else buf.loc(p.line, word_left_col(buf.line(b, p.line), p.col))
fn word_right(b, p) => if (p.col >= buf.line_len(b, p.line)) next_pos(b, p)
                       else buf.loc(p.line, word_right_col(buf.line(b, p.line), p.col))

fn indent_of(text) => slice(text, 0, len(text) - len(trim_start(text)))

// Home goes to the first non-blank column, then to column 0.
fn home_col(b, p) {
  let first = len(indent_of(buf.line(b, p.line)))
  if (p.col == first) 0 else first
}

// Vertical motion keeps the column it started from (`goal`).
fn vertical(b, sel, dl, extend) {
  let goal = if (sel.goal != null) sel.goal else sel.head.col
  let l = min(max(sel.head.line + dl, 0), b.count - 1)
  let head = if (sel.head.line + dl < 0) buf.loc(0, 0)
             else if (sel.head.line + dl >= b.count) buf.doc_end(b)
             else buf.loc(l, min(goal, buf.line_len(b, l)))
  {anchor: if (extend) sel.anchor else head, head: head, goal: goal}
}

// With soft wrap, Up and Down step by rows (design §6.4) and `goal` is the x
// the motion started from, in display columns within a row; on a row other
// than its line's last, the caret stops before the wrap point.
fn vertical_rows(b, vw, sel, n, extend) {
  let r = pos_row(b, vw, sel.head)
  let goal = if (sel.goal != null) sel.goal else display_col(buf.line(b, sel.head.line), sel.head.col) - r * vw.cols
  let to = row_step(b, vw, sel.head.line, r, n)
  let stuck = to.line == sel.head.line and to.row == r
  let last_row = to.row >= line_rows(b, vw, to.line) - 1
  let d = to.row * vw.cols + (if (last_row) goal else min(goal, vw.cols - 1))
  // a step that cannot move goes to the document's end, as Up and Down do unwrapped
  let head = if (stuck and n < 0) buf.loc(0, 0)
             else if (stuck and n > 0) buf.doc_end(b)
             else buf.loc(to.line, col_of_display(buf.line(b, to.line), d))
  {anchor: if (extend) sel.anchor else head, head: head, goal: goal}
}

// Up, Down and paging: by rows when wrapping, else by lines.
fn vertical_by(b, vw, sel, n, extend) =>
  if (wrapping(vw)) vertical_rows(b, vw, sel, n, extend) else vertical(b, sel, n, extend)

fn moved(sel, head, extend) => {anchor: if (extend) sel.anchor else head, head: head, goal: null}

// A collapsing arrow lands on the selection's edge, as in every editor.
fn horizontal(b, sel, forward, by_word, extend) {
  let edge = if (forward) sel_to(sel) else sel_from(sel)
  if (not extend and not collapsed(sel) and not by_word) caret(edge)
  else {
    let head = if (by_word) (if (forward) word_right(b, sel.head) else word_left(b, sel.head))
               else if (forward) next_pos(b, sel.head) else prev_pos(b, sel.head)
    moved(sel, head, extend)
  }
}

// The selection a navigation key produces, or null for other keys.
fn navigate(b, sel, vw, evt) {
  let key = evt.key
  let extend = evt.shiftKey == true
  let primary = evt.metaKey == true or evt.ctrlKey == true
  let word = evt.altKey == true
  let page = max(1, visible(vw) - 1)
  if (key == "ArrowLeft" and evt.metaKey == true) moved(sel, buf.loc(sel.head.line, home_col(b, sel.head)), extend)
  else if (key == "ArrowRight" and evt.metaKey == true) moved(sel, buf.loc(sel.head.line, buf.line_len(b, sel.head.line)), extend)
  else if (key == "ArrowUp" and primary) moved(sel, buf.loc(0, 0), extend)
  else if (key == "ArrowDown" and primary) moved(sel, buf.doc_end(b), extend)
  else if (key == "ArrowLeft") horizontal(b, sel, false, word or evt.ctrlKey == true, extend)
  else if (key == "ArrowRight") horizontal(b, sel, true, word or evt.ctrlKey == true, extend)
  else if (key == "ArrowUp") vertical_by(b, vw, sel, -1, extend)
  else if (key == "ArrowDown") vertical_by(b, vw, sel, 1, extend)
  else if (key == "PageUp") vertical_by(b, vw, sel, 0 - page, extend)
  else if (key == "PageDown") vertical_by(b, vw, sel, page, extend)
  else if (key == "Home" and primary) moved(sel, buf.loc(0, 0), extend)
  else if (key == "End" and primary) moved(sel, buf.doc_end(b), extend)
  else if (key == "Home") moved(sel, buf.loc(sel.head.line, home_col(b, sel.head)), extend)
  else if (key == "End") moved(sel, buf.loc(sel.head.line, buf.line_len(b, sel.head.line)), extend)
  else if (primary and lower(string(key)) == "a") {anchor: buf.loc(0, 0), head: buf.doc_end(b), goal: null}
  else null
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

// The range a delete intent removes from a collapsed caret.
fn delete_range(b, p, input_type) {
  if (input_type == "deleteContentBackward") {from: prev_pos(b, p), to: p}
  else if (input_type == "deleteContentForward") {from: p, to: next_pos(b, p)}
  else if (input_type == "deleteWordBackward") {from: word_left(b, p), to: p}
  else if (input_type == "deleteWordForward") {from: p, to: word_right(b, p)}
  else if (input_type == "deleteSoftLineBackward" or input_type == "deleteHardLineBackward")
    {from: if (p.col == 0) prev_pos(b, p) else buf.loc(p.line, 0), to: p}
  else if (input_type == "deleteSoftLineForward" or input_type == "deleteHardLineForward")
    {from: p, to: if (p.col >= buf.line_len(b, p.line)) next_pos(b, p) else buf.loc(p.line, buf.line_len(b, p.line))}
  else {from: p, to: p}
}

let delete_types = ["deleteContentBackward", "deleteContentForward", "deleteWordBackward",
                    "deleteWordForward", "deleteSoftLineBackward", "deleteSoftLineForward",
                    "deleteHardLineBackward", "deleteHardLineForward", "deleteByCut", "deleteByDrag"]

let insert_types = ["insertText", "insertReplacementText", "insertFromPaste", "insertFromDrop",
                    "insertFromYank", "insertLineBreak", "insertParagraph"]

// Indentation a new line inherits from the line it splits.
fn newline_lines(b, from) => ["", indent_of(slice(buf.line(b, from.line), 0, from.col))]

// The edit an input intent asks for: {from, to, insert} or null.
fn intent_edit(b, sel, input_type, data) {
  let from = sel_from(sel)
  let to = sel_to(sel)
  if (input_type == "insertLineBreak" or input_type == "insertParagraph")
    buf.delta(from, to, newline_lines(b, from))
  else if (contains(insert_types, input_type))
    buf.delta(from, to, buf.text_lines(replace(string(data or ""), "\r\n", "\n")))
  else if (contains(delete_types, input_type)) {
    let r = if (collapsed(sel)) delete_range(b, from, input_type) else {from: from, to: to}
    if (buf.pos_cmp(r.from, r.to) == 0) null else buf.delta(r.from, r.to, [""])
  }
  else null
}

// A pause this long between two keystrokes starts a new undo step.
let TYPING_GAP_MS = 500.0

// Typing one character next to the previous one extends its undo step until a
// word boundary, a pause or a caret move (design §5.2); the first character
// may replace a selection, so typing over it undoes at once.
fn typed_char(d) => len(d.insert) == 1 and len(d.insert[0]) == 1

fn joins_last(hist, d, now) {
  if (len(hist.undo) == 0 or not typed_char(d) or buf.pos_cmp(d.from, d.to) != 0) false
  else {
    let prev = hist.undo[len(hist.undo) - 1]
    prev.typing and buf.pos_cmp(prev.after.head, d.from) == 0 and is_word_char(d.insert[0]) and
      now != null and prev.at != null and now - prev.at < TYPING_GAP_MS
  }
}

// The history after the caret moved on its own: the last typing step is over.
fn closed(hist) {
  let n = len(hist.undo)
  if (n == 0 or not hist.undo[n - 1].typing) hist
  else {*: hist, undo: [*take(hist.undo, n - 1), {*: hist.undo[n - 1], typing: false}]}
}

// Apply one delta: the new buffer, selection, and history.
fn edit_step(st, d) {
  let r = buf.apply_delta(st.b, d)
  let head = buf.insert_end(d.from, d.insert)
  let after = caret(head)
  let entry = {inverses: [r.inverse], before: st.sel, after: after, typing: typed_char(d), at: st.now}
  let undo = if (joins_last(st.hist, d, st.now)) {
               let prev = st.hist.undo[len(st.hist.undo) - 1];
               [*take(st.hist.undo, len(st.hist.undo) - 1),
                {*: prev, inverses: [*prev.inverses, r.inverse], after: after, at: st.now}]
             }
             else [*st.hist.undo, entry]
  {b: r.buf, sel: after, hist: {undo: undo, redo: []}, steps: [{delta: d, applied: r}]}
}

// Undo pops from one stack and pushes its inverse on the other; redo is the
// same move in the other direction.
fn replay(b, inverses, i, acc, steps) {
  if (i < 0) {b: b, inverses: acc, steps: steps}
  else {
    let r = buf.apply_delta(b, inverses[i])
    replay(r.buf, inverses, i - 1, [*acc, r.inverse], [*steps, {delta: inverses[i], applied: r}])
  }
}

fn history_step(st, undoing) {
  let from_stack = if (undoing) st.hist.undo else st.hist.redo
  if (len(from_stack) == 0) null
  else {
    let e = from_stack[len(from_stack) - 1]
    let rest = take(from_stack, len(from_stack) - 1)
    let r = replay(st.b, e.inverses, len(e.inverses) - 1, [], [])
    let back = {inverses: r.inverses, before: e.after, after: e.before, typing: false}
    let to_stack = [*(if (undoing) st.hist.redo else st.hist.undo), back]
    {b: r.b, sel: e.before, steps: r.steps,
     hist: if (undoing) {undo: rest, redo: to_stack} else {undo: to_stack, redo: rest}}
  }
}

// IME composition (design §9). Each update replaces the composing text
// without touching history; the commit is one undo step against the buffer as
// it was before composing began, and a cancel restores that buffer. Both take
// a fresh version, since dirty state compares versions (same_doc).
let composition_types = ["compositionStart", "insertCompositionText", "insertFromComposition",
                         "deleteCompositionText"]

fn rebased(base_b, b) => {*: base_b, version: b.version + 1}

fn composition_step(st, comp, input_type, data) {
  if (input_type == "compositionStart")
    {b: st.b, sel: st.sel, hist: st.hist, steps: [],
     comp: {base: st.b, base_sel: st.sel, base_hist: st.hist, from: sel_from(st.sel), to: sel_to(st.sel)}}
  else if (input_type == "insertFromComposition") {
    let base = if (comp == null) st else {b: rebased(comp.base, st.b), sel: comp.base_sel, hist: comp.base_hist, now: st.now}
    let committed = edit_step(base, buf.delta(sel_from(base.sel), sel_to(base.sel), buf.text_lines(string(data or ""))));
    // the commit applies to the pre-composition buffer: drop the mapped runs
    {*: committed, steps: null}
  }
  else if (comp == null) null
  else if (input_type == "insertCompositionText") {
    let d = buf.delta(comp.from, comp.to, buf.text_lines(string(data or "")))
    let stop = buf.insert_end(d.from, d.insert)
    let r = buf.apply_delta(st.b, d);
    {b: r.buf, sel: caret(stop), hist: st.hist, comp: {*: comp, to: stop}, steps: [{delta: d, applied: r}]}
  }
  else {b: rebased(comp.base, st.b), sel: comp.base_sel, hist: comp.base_hist, comp: null, steps: null}
}

// Tab indents with the file's own unit: a tab if a line starts with one,
// else two spaces.
fn indent_unit(b) =>
  if (any([for (l in buf.lines(b, 0, 200)) starts_with(l, "\t")]) or false) "\t" else "  "

// The lines a re-indent covers: a selection ending at column 0 leaves its
// last line alone, as in every editor.
fn span_last(sel) {
  let from = sel_from(sel)
  let to = sel_to(sel)
  if (to.line > from.line and to.col == 0) to.line - 1 else to.line
}

// Leading whitespace one dedent removes: a tab, else up to one unit of spaces.
fn dedent_width(t, unit) {
  let width = if (unit == "\t") 4 else len(unit)
  if (starts_with(t, "\t")) 1
  else len(slice(t, 0, width)) - len(trim_start(slice(t, 0, width)))
}

// Tab over lines and Shift+Tab re-indent whole lines as one undo step; the
// selection keeps covering the same text.
fn reindent_step(st, unit, indent) {
  let first = sel_from(st.sel).line
  let final = span_last(st.sel)
  let old = buf.lines(st.b, first, final - first + 1)
  let removed = [for (t in old) if (indent) 0 else dedent_width(t, unit)]
  let added = [for (t in old) if (indent and len(t) > 0) len(unit) else 0]
  if (sum(removed) + sum(added) == 0) null
  else {
    let lines = [for (i in 0 to len(old) - 1)
                   if (indent and added[i] > 0) unit ++ old[i] else slice(old[i], removed[i], len(old[i]))]
    let step = edit_step(st, buf.delta(buf.loc(first, 0), buf.loc(final, len(old[len(old) - 1])), lines))
    let moved = (p) => if (p.line < first or p.line > final) p
                       else if (indent) buf.loc(p.line, if (p.col > 0) p.col + added[p.line - first] else 0)
                       else buf.loc(p.line, max(0, p.col - removed[p.line - first]))
    let after = {anchor: moved(st.sel.anchor), head: moved(st.sel.head), goal: null}
    let undo = step.hist.undo;
    {*: step, sel: after,
     hist: {undo: [*take(undo, len(undo) - 1), {*: undo[len(undo) - 1], after: after}], redo: []}}
  }
}

// ---------------------------------------------------------------------------
// Highlighting (CED14v2): pass 1 maps the runs through an edit while the
// handler runs; pass 2 makes the window exact on the next frame (OQ16), so
// the parser is never on the keystroke path. One frame request is outstanding
// at a time (`frame` holds its token), so a burst of keys costs one parse.
// ---------------------------------------------------------------------------

// Rows parsed beyond the visible window, so ordinary scrolling stays inside it.
fn highlight_pad(rows) => 2 * rows

fn new_highlight(path) => {lang: syn.language_of(path), hl: null, scan: syn.empty_scan(), find: null}

// Pass 1. `steps` are the edits applied since the last settle ([] for none,
// null when the runs no longer describe the buffer).
fn settle(hs, steps, b) =>
  if (steps != null and len(steps) == 0) hs
  else {*: hs, *: syn.after_steps(hs.hl, hs.scan, steps, b.version),
        // an edit leaves a caret, which is no match: recount only
        find: if (hs.find == null) null else find_state(b, caret(buf.loc(0, 0)), hs.find.query)}

// Whether the rendered window has rows that are plain or provisional.
fn needs_exact(hs, b, vw) => hs.lang != null and not syn.covers(hs.hl, b, vw.top, vw.top + vw.rows - 1)

// Pass 2: parse the window with its padding and swap the exact runs in.
fn exact(hs, b, vw) {
  if (not needs_exact(hs, b, vw)) hs
  else {
    let pad = highlight_pad(vw.rows)
    let r = syn.highlight(b, vw.top - pad, vw.top + vw.rows + pad, hs.scan, hs.lang)
    if (r == null) hs else {*: hs, hl: r.hl, scan: r.scan}
  }
}

// Ask for pass 2 on the next frame, unless one is already asked for or the
// window is exact; returns the outstanding request's token (0 for none). The
// frame event targets the body, the element this template renders.
pn request_exact(node, hs, b, vw, token) {
  if (token != 0 or not needs_exact(hs, b, vw)) { return token }
  let body = dom.query_selector(dom.root_node(node), "body")
  if (body == null) { return 0 }
  dom.request_frame(body, "source_frame")
}

// ---------------------------------------------------------------------------
// Find (Cmd/Ctrl+F). Matches ignore case. The open query lives in the paint
// state, so matches in the window render as marked runs over the tokens.
// ---------------------------------------------------------------------------

// A line's matches as marks for `syn.overlay`.
fn find_marks(t, f) {
  if (f == null or f.query == "") []
  else [for (m in find(lower(t), lower(f.query))) {s: m.index, e: m.index + len(f.query)}]
}

fn count_in(t, lq) => len(find(lower(t), lq))

// Matches per chunk: one native search over each chunk's joined lines.
fn chunk_counts(b, lq) => [for (c in b.chunks) count_in(join(c, "\n"), lq)]

fn is_match(b, sel, lq) =>
  not collapsed(sel) and sel_from(sel).line == sel_to(sel).line and
  lower(buf.text_between(b, sel.anchor, sel.head)) == lq

// The open query, its match count, and which match the selection is (0 for
// none).
fn find_state(b, sel, q) {
  if (q == "") {query: q, total: 0, index: 0}
  else {
    let lq = lower(q)
    let per = chunk_counts(b, lq)
    let from = sel_from(sel)
    let k = buf.chunk_of(b, from.line)
    let c = b.chunks[k]
    let row = from.line - b.starts[k]
    let above = if (row == 0) 0 else count_in(join(take(c, row), "\n"), lq)
    let left = len([for (m in find(lower(c[row]), lq) where m.index < from.col) m])
    {query: q, total: sum(per),
     index: if (is_match(b, sel, lq)) sum(take(per, k)) + above + left + 1 else 0}
  }
}

fn match_sel(line, col, n) => {anchor: buf.loc(line, col), head: buf.loc(line, col + n), goal: null}

// The first match at or after (line, col), wrapping past the end; null for none.
fn next_match(b, lq, line, col, steps) {
  if (steps > b.count) null
  else {
    let t = lower(buf.line(b, line))
    let i = index_of(slice(t, col, len(t)), lq)
    if (i != null) match_sel(line, col + i, len(lq))
    else next_match(b, lq, if (line + 1 >= b.count) 0 else line + 1, 0, steps + 1)
  }
}

// The last match ending at or before (line, col), wrapping past the start.
fn prev_match(b, lq, line, col, steps) {
  if (steps > b.count) null
  else {
    let i = last_index_of(slice(lower(buf.line(b, line)), 0, col), lq)
    if (i != null) match_sel(line, i, len(lq))
    else {
      let up = if (line == 0) b.count - 1 else line - 1
      prev_match(b, lq, up, buf.line_len(b, up), steps + 1)
    }
  }
}

// The selection, window and paint state after a find step from `from`, or
// null when the query matches nothing.
fn find_moved(b, vw, hs, q, forward, from) {
  let found = if (q == "") null
              else if (forward) next_match(b, lower(q), from.line, from.col, 0)
              else prev_match(b, lower(q), from.line, from.col, 0)
  if (found == null) null
  else {
    let v = follow(b, vw, found.head)
    {sel: found, vw: v, hs: {*: hs, find: find_state(b, found, q)}}
  }
}

fn find_label(f) =>
  if (f.query == "") "" else if (f.total == 0) "No results"
  else if (f.index == 0) string(f.total) ++ " matches"
  else string(f.index) ++ " of " ++ string(f.total)

// The bar floats over the text's top right, so opening it moves no rows.
// `autofocus` takes focus once the surface has given it up (Cmd+F).
fn find_bar(f) =>
  <div class: "src-find", role: "search",
    <input id: "src-find-input", class: "src-find-input", type: "text", value: f.query,
           placeholder: "Find", spellcheck: "false", autofocus: "autofocus">
    <span class: "src-find-count", find_label(f)>
    <button class: "src-find-btn src-find-prev", title: "Previous match (Shift+Enter)", "Prev">
    <button class: "src-find-btn src-find-next", title: "Next match (Enter)", "Next">
    <button class: "src-find-btn src-find-close", title: "Close (Escape)", "Close">
  >

// ---------------------------------------------------------------------------
// The application template
// ---------------------------------------------------------------------------

// The selection's size: characters within a line, lines across several (a
// character count would walk every selected line on each render).
fn selection_label(sel) {
  let from = sel_from(sel)
  let to = sel_to(sel)
  // a selection that ends at a line's start does not include that line
  let lines = to.line - from.line + (if (to.col == 0) 0 else 1);
  if (collapsed(sel)) null
  else if (from.line == to.line) string(to.col - from.col) ++ " selected"
  else if (lines == 1) "1 line selected"
  else string(lines) ++ " lines selected"
}

fn language_label(lang) => if (lang == 'markdown') "Markdown" else if (lang == 'html') "HTML" else "Plain Text"

// Design §10: the caret, the selection's size and the last message on the
// left; the language, line ends and dirty state on the right.
fn status_line(b, sel, hs, status, dirty) {
  let where = "Ln " ++ string(sel.head.line + 1) ++ ", Col " ++ string(sel.head.col + 1)
  let left = [for (part in [where, selection_label(sel), status] where part != null and part != "") part]
  let right = [for (part in [language_label(hs.lang), if (b.eol == "\r\n") "CRLF" else "LF",
                             if (dirty) "Unsaved" else null] where part != null) part];
  <div class: "edit-status", role: "status",
    <span class: "src-status-left", join(left, "  ·  ")>
    <span class: "src-status-right", join(right, "  ·  ")>
  >
}

// The toolbar's history buttons query this record instead of a rich editor.
fn history_probe(hist) => {kind: 'source', can_undo: len(hist.undo) > 0, can_redo: len(hist.redo) > 0}

let source_css = "
  /* the page is exactly the window: the source surface scrolls itself
     (design §6.2), so nothing below the status line may grow the body */
  body.edit-format-source { margin: 0; height: 100vh; overflow: hidden; display: flex;
                            flex-direction: column;
                            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
  .edit-format-source .edit-status { flex: none; display: flex; justify-content: space-between; gap: 12px;
                                     padding: 4px 12px; font-size: 12px; color: #57606a;
                                     background: #f6f8fa; border-top: 1px solid #d0d7de; }
  .src-status-left { white-space: pre; overflow: hidden; }
  .src-status-right { flex: none; white-space: pre; }
  .src-main { flex: 1; min-height: 0; display: flex; overflow: hidden;
              font-family: 'SF Mono', Menlo, Monaco, Consolas, monospace; font-size: 13px;
              background: #ffffff; color: #1f2328; }
  .src-gutter { flex: none; padding: 4px 10px 0 12px; text-align: right; color: #8c959f;
                background: #f6f8fa; border-right: 1px solid #d8dee4; user-select: none;
                position: relative; z-index: 1; cursor: default; }
  .src-num { height: 20px; line-height: 20px; }
  .src-text { flex: 1; min-width: 0; padding: 4px 0 0 10px; outline: none; overflow: hidden;
              white-space: pre; }
  .src-text .edit-doc > div { height: 20px; line-height: 20px; white-space: pre; }
  /* soft wrap breaks at any character, so a line's rows are its columns over
     the width (design §6.4) */
  .src-text.src-wrap .edit-doc > div { height: auto; min-height: 20px; white-space: pre-wrap;
                                      word-break: break-all; }
  .src-measure { position: absolute; left: 0; top: 0; visibility: hidden; white-space: pre; }
  .src-scroll { flex: none; position: relative; width: 12px; background: #f6f8fa;
                border-left: 1px solid #d8dee4; cursor: default; }
  .src-thumb { position: absolute; left: 2px; right: 2px; min-height: 8px; border-radius: 4px;
               background: #c4c9cf; }
  .src-main { position: relative; }
  .src-find { position: absolute; top: 0; right: 24px; z-index: 5; display: flex; align-items: center;
              gap: 6px; padding: 6px 8px; background: #f6f8fa; border: 1px solid #d0d7de;
              border-top: none; border-radius: 0 0 6px 6px; box-shadow: 0 4px 12px rgba(0,0,0,0.12);
              font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; font-size: 12px; }
  .src-find-input { width: 220px; padding: 3px 6px; border: 1px solid #d0d7de; border-radius: 4px;
                    font-size: 13px; }
  .src-find-count { min-width: 72px; color: #57606a; }
  .src-find-btn { height: 24px; line-height: 22px; padding: 0 8px; border: 1px solid #d0d7de; border-radius: 4px;
                  background: #ffffff; font-size: 12px; }
  /* a match keeps its token's color: only the background changes (design §7.5) */
  .src-match { background: #fff8c5; }
"

// The surface's own stylesheet; the edit application adds the shared ones.
pub let surface_css = source_css ++ syn.css

// The source surface as the edit application's body. Opening highlights the
// first window at once: at the top of the file the parse starts at line 0 with
// no restart scan, so it costs one pass 2 whatever the file's size.
pub fn app(session, b, status) =>
  apply(<source_app session: session, buf: b, status: status,
                    hs: exact(new_highlight(session.path), b, new_view())>, {mode: "edit"})

// ---------------------------------------------------------------------------
// Switching views (OQ7, CED20): the application shows the rich surface or
// this one over the same file. A switch carries the current text; an unsaved
// text stays dirty because the new session's checkpoint is the disk text.
// ---------------------------------------------------------------------------

// The source view of `text` for the file `session` edits.
pub fn source_view(session, text, dirty) {
  let doc = buf.from_text(text)
  let saved = if (dirty) buf.from_text(session.disk_text) else doc
  // dirty state compares buffer versions (same_doc): the unsaved text is one
  // edit ahead of the disk's
  let current = if (dirty) {*: doc, version: saved.version + 1} else doc
  let s = {*: sess.new_session(session.path, descriptor, session.disk_text, {doc: saved, envelope: null}),
           rich_format: session.rich_format};
  {mode: 'source', session: s, doc: current, status: "Source view."}
}

// The rich view of the buffer, or {status} saying why its format cannot keep
// the text exactly (the rule that refuses such a file at open,
// Radiant_Design_Edit_Mode §2).
pn rich_view(session, b) {
  let format = session.rich_format
  let text = buf.to_text(b)
  let loaded = format.import_text(text) ^ { {error: ^.message} }
  if (loaded.error != null) { return {status: "Cannot show the " ++ format.name ++ " view: " ++ loaded.error} }
  let mismatch = format.check_roundtrip(loaded.doc, loaded.envelope)
  if (mismatch != null) {
    return {status: "The " ++ format.name ++ " view cannot keep this text exactly: " ++ mismatch}
  }
  let saved = if (not sess.is_dirty(session, b)) loaded.doc
              else (format.import_text(session.disk_text) ^ { {doc: null} }).doc
  let s = {*: sess.new_session(session.path, format, session.disk_text, {doc: saved, envelope: loaded.envelope}),
           rich_format: format}
  {mode: 'rich', session: s, doc: edit_open(loaded.doc, format.schema, null), status: ""}
}

// The surface's rows, and its width in characters from the measuring span's
// advance (soft wrap): on mount and whenever the window resizes.
pn measured(vw, root) {
  let surface = dom.get_element_by_id(root, "edit-surface")
  let r = if (surface == null) null else dom.bounding_box(surface)
  let probe = dom.get_element_by_id(root, "src-measure")
  let m = if (probe == null) null else dom.bounding_box(probe)
  let char_w = if (m != null and m.width > 0) m.width / float(MEASURE_CHARS) else 0.0
  // a column scrolled out to the left widens the surface's box by as much
  {*: vw, rows: if (r != null and r.height > 0) rows_for(r.height) else vw.rows,
   cols: if (r != null and char_w > 0.0) int((r.width - TEXT_PAD - left_px(vw)) / char_w) else vw.cols,
   char_w: if (char_w > 0.0) char_w else vw.char_w}
}

// Bind the surface once and measure it.
pn mounted(vw, node, rev) {
  if (vw.handle != null) { return vw }
  let root = dom.root_node(node)
  let surface = dom.get_element_by_id(root, "edit-surface")
  if (surface == null) { return vw }
  {*: measured(vw, root), handle: dom.bind_model_edit_surface(surface, rev)}
}

// Put the native selection where the model has it once this render lands;
// returns the model revision the request carries.
pn project(vw, b, hs, sel, rev) {
  let next = rev + 1
  if (vw.handle != null) {
    dom.finish_model_edit(vw.handle,
      edit_result.model_applied(false, true, false, "", source_selection_of(b, vw, hs, sel), next))
  }
  next
}

// One keyboard edit (Tab) as a state step.
fn typed_step(st, text) => edit_step(st, buf.delta(sel_from(st.sel), sel_to(st.sel), [text]))

edit <source_app> state session: ~.session, b: ~.buf, status: ~.status, sel: caret(buf.loc(0, 0)),
                         vw: new_view(), hist: {undo: [], redo: []},
                         rev: 0, comp: null, hs: ~.hs, frame: 0, wheel_px: 0.0, wheel_dx: 0.0, drag: null, dialog: null,
                         after_save: null {
  let dirty = sess.is_dirty(session, b);
  <body class: "edit-app edit-format-source",
    <div class: "edit-toolbar", role: "toolbar", ["aria-label"]: "Document",
      files.file_label(session, dirty)
      tools.group(tools.file_group, history_probe(hist), dirty, null)
      if (session.rich_format != null) tools.group(tools.rich_view_group, history_probe(hist), dirty, null) else null
    >
    <div class: "src-main",
      gutter(b, vw);
      <div id: "edit-surface", class: if (vw.wrap) "src-text src-wrap" else "src-text", contenteditable: "true",
           spellcheck: "false", tabindex: "0", style: surface_style(vw), apply(window_doc(b, vw, hs))>
      scrollbar(b, vw);
      <span id: "src-measure", class: "src-measure", ["aria-hidden"]: "true", MEASURE_TEXT>
      if (hs.find != null) find_bar(hs.find) else null
    >
    status_line(b, sel, hs, status, dirty)
    files.dialog_view(dialog, session)
  >
}
on beforeinput(evt) {
  return 'pass'
}
on editaction(evt) {
  vw = mounted(vw, evt.target, rev)
  let input_type = evt.input_type
  let target = action_selection(b, vw, hs, sel, evt)
  // The DOM shows only the window's part of the selection, so copy and cut
  // hand Radiant the model's text to put on the clipboard.
  let copied = clipboard_text(b, target)
  if (input_type == "copy") {
    return edit_result.with_clipboard_text(
      edit_result.model_applied(false, false, false, "", source_selection_of(b, vw, hs, sel), rev), copied)
  }
  let history = input_type == "historyUndo" or input_type == "historyRedo"
  let composing = contains(composition_types, input_type)
  let st = {b: b, sel: target, hist: hist, now: evt.time_stamp}
  let next = if (composing) composition_step(st, comp, input_type, evt.data)
             else if (history) history_step(st, input_type == "historyUndo")
             else {
               let d = intent_edit(b, target, input_type, evt.data)
               if (d == null) null else edit_step(st, d)
             }
  if (next == null) {
    let known = history or composing or contains(insert_types, input_type) or contains(delete_types, input_type)
    return if (known) edit_result.model_applied(false, false, false, "", source_selection_of(b, vw, hs, sel), rev)
           else edit_result.decline(true, false, "unsupported", 0)
  }
  b = next.b
  sel = next.sel
  hist = next.hist
  comp = next.comp
  vw = follow(b, vw, sel.head)
  hs = settle(hs, next.steps, b)
  frame = request_exact(evt.target, hs, b, vw, frame)
  rev = rev + 1
  status = ""
  files.sync_window(evt.target, session, b)
  let applied = edit_result.model_applied(true, true, true, "", source_selection_of(b, vw, hs, sel), rev)
  if (input_type == "deleteByCut") edit_result.with_clipboard_text(applied, copied) else applied
}
on selectionchange(evt) {
  let picked = model_selection(b, vw.top, hs, evt.source_selection)
  // the selection the surface itself projected keeps the model's own
  if (picked != null and evt.source_selection != source_selection_of(b, vw, hs, sel)) {
    sel = picked
    hist = closed(hist)
    // the status redraw replaces the rows the native caret is in
    rev = project(vw, b, hs, sel, rev)
  }
}
on edit_cmd(req) {
  vw = mounted(vw, req.node, rev)
  let cmd = req.cmd
  if (cmd == "view_rich") {
    let next = rich_view(session, b)
    if (next.mode != null) {
      emit("edit_switch", next)
      return
    }
    status = next.status
    return
  }
  if (cmd == "save") {
    let saved = files.save_now(session, b, req.node)
    session = saved.session
    status = saved.status
    if (saved.dialog != null) { dialog = saved.dialog }
    else { rev = project(vw, b, hs, sel, rev) }
  }
  else if (cmd == "save_as") { dialog = {kind: 'save_as', path: session.path} }
  else if (cmd == "undo" or cmd == "redo") {
    let next = history_step({b: b, sel: sel, hist: hist}, cmd == "undo")
    if (next != null) {
      b = next.b
      sel = next.sel
      hist = next.hist
      vw = follow(b, vw, sel.head)
      hs = settle(hs, next.steps, b)
      frame = request_exact(req.node, hs, b, vw, frame)
      files.sync_window(req.node, session, b)
    }
    files.focus_surface(req.node)
    rev = project(vw, b, hs, sel, rev)
  }
}
on edit_dialog(req) {
  let filed = files.dialog_action(req.action, req.node,
    {session: session, doc: b, status: status, dialog: dialog, after_save: after_save})
  session = filed.session
  status = filed.status
  dialog = filed.dialog
  after_save = filed.after_save
  if (filed.reloaded != null) {
    b = filed.reloaded
    sel = caret(buf.loc(0, 0))
    vw = {*: vw, top: 0, row: 0}
    hist = {undo: [], redo: []}
    hs = new_highlight(session.path)
    frame = request_exact(req.node, hs, b, vw, frame)
  }
  if (dialog == null) {
    files.focus_surface(req.node)
    rev = project(vw, b, hs, sel, rev)
  }
}
on keydown(evt) {
  vw = mounted(vw, evt.target, rev)
  let primary = evt.metaKey == true or evt.ctrlKey == true
  if (primary and lower(string(evt.key)) == "s") {
    if (evt.shiftKey == true) { dialog = {kind: 'save_as', path: session.path} }
    else {
      let saved = files.save_now(session, b, evt.target)
      session = saved.session
      status = saved.status
      // the status redraw replaces the rows the native selection is in
      if (saved.dialog != null) { dialog = saved.dialog }
      else { rev = project(vw, b, hs, sel, rev) }
    }
    return 'prevent-default'
  }
  if (dialog != null) {
    if (evt.key != "Escape") { return 'pass' }
    dialog = null
    after_save = null
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  // Cmd/Ctrl+/ switches to the rich view (OQ7), as Typora's source mode does
  if (primary and evt.key == "/" and session.rich_format != null) {
    let next = rich_view(session, b)
    if (next.mode != null) {
      emit("edit_switch", next)
      return 'prevent-default'
    }
    status = next.status
    return 'prevent-default'
  }
  let find_key = primary and lower(string(evt.key)) == "g"
  // keys typed into the find field edit it natively, except the find keys
  if (hs.find != null and dom.matches(evt.target, "#src-find-input")) {
    if (evt.key == "Escape") {
      hs = {*: hs, find: null}
      files.focus_surface(evt.target)
      rev = project(vw, b, hs, sel, rev)
      return 'prevent-default'
    }
    if (evt.key != "Enter" and not find_key) { return 'pass' }
    let forward = evt.shiftKey != true
    let moved = find_moved(b, vw, hs, hs.find.query, forward, if (forward) sel_to(sel) else sel_from(sel))
    if (moved != null) {
      sel = moved.sel
      vw = moved.vw
      hs = moved.hs
      frame = request_exact(evt.target, hs, b, vw, frame)
      rev = project(vw, b, hs, sel, rev)
    }
    return 'prevent-default'
  }
  // a click places the caret without a selectionchange; adopt it first
  let adopted = action_selection(b, vw, hs, sel, evt)
  // Alt+Z toggles soft wrap (as in VS Code), keeping the caret in view
  if (evt.altKey == true and not primary and lower(string(evt.key)) == "z") {
    // a vertical motion's goal is a column unwrapped and an x within a row wrapped
    sel = {*: adopted, goal: null}
    vw = follow(b, {*: vw, wrap: not vw.wrap, row: 0}, adopted.head)
    frame = request_exact(evt.target, hs, b, vw, frame)
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  // Cmd+F opens find seeded with a one-line selection, then hands focus to
  // the field: its autofocus applies once the surface has none
  if (primary and lower(string(evt.key)) == "f") {
    let seed = if (not collapsed(adopted) and sel_from(adopted).line == sel_to(adopted).line)
                 buf.text_between(b, adopted.anchor, adopted.head)
               else if (hs.find != null) hs.find.query else ""
    sel = adopted
    hs = {*: hs, find: find_state(b, sel, seed)}
    rev = project(vw, b, hs, sel, rev)
    dom.clear_editing_focus(dom.get_element_by_id(dom.root_node(evt.target), "edit-surface"))
    return 'prevent-default'
  }
  if (find_key and hs.find != null) {
    let forward = evt.shiftKey != true
    let moved = find_moved(b, vw, hs, hs.find.query, forward, if (forward) sel_to(adopted) else sel_from(adopted))
    sel = if (moved == null) adopted else moved.sel
    if (moved != null) {
      vw = moved.vw
      hs = moved.hs
      frame = request_exact(evt.target, hs, b, vw, frame)
    }
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  let st = {b: b, sel: adopted, hist: hist, now: evt.time_stamp}
  let undo_key = primary and lower(string(evt.key)) == "z"
  let next = if (undo_key) history_step(st, evt.shiftKey != true)
             else if (evt.key == "Tab" and evt.shiftKey == true) reindent_step(st, indent_unit(b), false)
             else if (evt.key == "Tab" and sel_from(adopted).line != sel_to(adopted).line) reindent_step(st, indent_unit(b), true)
             else if (evt.key == "Tab") typed_step(st, indent_unit(b))
             else null
  let moved_sel = if (next == null) navigate(b, adopted, vw, evt) else null
  if (next == null and moved_sel == null) {
    // A key this surface does not handle changes no state: the render would
    // replace the rows its native default (paste) is about to target, and the
    // editaction that follows adopts the DOM selection itself. An undo with
    // nothing to undo, or a dedent with nothing to remove, is still ours: Tab
    // must not move focus away.
    if (not (undo_key or evt.key == "Tab")) { return 'pass' }
    sel = adopted
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  if (next != null) {
    b = next.b
    sel = next.sel
    hist = next.hist
    files.sync_window(evt.target, session, b)
  }
  else {
    sel = moved_sel
    hist = closed(hist)
  }
  vw = follow(b, vw, sel.head)
  hs = settle(hs, if (next == null) [] else next.steps, b)
  frame = request_exact(evt.target, hs, b, vw, frame)
  rev = project(vw, b, hs, sel, rev)
  'prevent-default'
}
// The editor owns its scroll position (CED13): the wheel moves the window by
// lines, and the selection is projected again because rows changed under it.
on wheel(evt) {
  if (dialog != null) { return 'pass' }
  vw = mounted(vw, evt.target, rev)
  let step = wheel_step(wheel_px + float(evt.deltaY or 0))
  wheel_px = step.rest
  // sideways motion moves whole columns; the remainder carries like lines
  let dx = wheel_dx + float(evt.deltaX or 0)
  let cols = if (vw.char_w > 0.0) int(dx / vw.char_w) else 0
  wheel_dx = dx - float(cols) * vw.char_w
  let next = scrolled_left(b, scrolled(b, vw, step.lines), cols)
  if (next.top != vw.top or next.row != vw.row or next.left != vw.left) {
    vw = next
    frame = request_exact(evt.target, hs, b, vw, frame)
    rev = project(vw, b, hs, sel, rev)
  }
  'prevent-default'
}
// A press on the thumb starts a drag; a press on the track pages toward it.
// Shift+press extends the model selection: its anchor may lie outside the
// window, where the clamped DOM selection cannot extend from it.
on mousedown(evt) {
  // A press on a line number selects the line, as in every editor; Shift
  // extends the selection to it (design §6.3).
  let num = if (dom.node_type(evt.target) == 1) evt.target else dom.parent_node(evt.target)
  if (dialog == null and num != null and dom.node_type(num) == 1 and dom.matches(num, ".src-num")) {
    vw = mounted(vw, evt.target, rev)
    let l = int(dom.get_attribute(num, "data-line"))
    let start = buf.loc(l, 0)
    let stop = if (l + 1 < b.count) buf.loc(l + 1, 0) else buf.loc(l, buf.line_len(b, l))
    sel = if (evt.shiftKey != true) {anchor: start, head: stop, goal: null}
          else {anchor: sel.anchor, head: if (buf.pos_cmp(start, sel.anchor) < 0) start else stop, goal: null}
    hist = closed(hist)
    vw = follow(b, vw, sel.head)
    frame = request_exact(evt.target, hs, b, vw, frame)
    files.focus_surface(evt.target)
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  if (evt.shiftKey == true and evt.source_pos != null and dialog == null) {
    vw = mounted(vw, evt.target, rev)
    sel = {anchor: sel.anchor, head: from_source(b, vw.top, hs, evt.source_pos), goal: null}
    hist = closed(hist)
    vw = follow(b, vw, sel.head)
    frame = request_exact(evt.target, hs, b, vw, frame)
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  if (dom.node_type(evt.target) != 1) { return 'pass' }
  let on_thumb = dom.matches(evt.target, ".src-thumb")
  if (not on_thumb and not dom.matches(evt.target, ".src-scroll")) { return 'pass' }
  let track = if (on_thumb) dom.parent_node(evt.target) else evt.target
  let r = dom.bounding_box(track)
  if (on_thumb) { drag = {y: evt.y, top: vw.top, height: r.height} }
  else {
    let thumb_y = r.top + r.height * float(vw.top) / float(max(1, b.count))
    let page = max(1, visible(vw) - 1)
    vw = scrolled(b, vw, if (evt.y < thumb_y) 0 - page else page)
    frame = request_exact(evt.target, hs, b, vw, frame)
  }
  'prevent-default'
}
// The thumb follows the pointer: its travel over the track is the document's.
on mousemove(evt) {
  if (drag == null) { return 'pass' }
  let lines = int((evt.y - drag.y) * float(b.count) / max(1.0, drag.height))
  let next = scrolled_to(b, vw, drag.top + lines)
  if (next.top != vw.top or next.row != vw.row) {
    vw = next
    frame = request_exact(evt.target, hs, b, vw, frame)
  }
  'prevent-default'
}
on mouseup(evt) {
  if (drag == null) { return 'pass' }
  drag = null
  rev = project(vw, b, hs, sel, rev)
  'prevent-default'
}
// Typing in the find field searches as you type, from the selection's start
// so a match already selected stays selected.
on input(evt) {
  if (hs.find == null or dom.node_type(evt.target) != 1 or not dom.matches(evt.target, "#src-find-input")) { return }
  let q = string(dom.get_state(evt.target, "value") or "")
  let moved = find_moved(b, vw, hs, q, true, sel_from(sel))
  if (moved == null) { hs = {*: hs, find: find_state(b, sel, q)} }
  else {
    sel = moved.sel
    vw = moved.vw
    hs = moved.hs
    frame = request_exact(evt.target, hs, b, vw, frame)
  }
  rev = project(vw, b, hs, sel, rev)
}
on click(evt) {
  if (hs.find == null or dom.node_type(evt.target) != 1) { return 'pass' }
  if (dom.matches(evt.target, ".src-find-close")) {
    hs = {*: hs, find: null}
    files.focus_surface(evt.target)
    rev = project(vw, b, hs, sel, rev)
    return 'prevent-default'
  }
  let forward = dom.matches(evt.target, ".src-find-next")
  if (not forward and not dom.matches(evt.target, ".src-find-prev")) { return 'pass' }
  let moved = find_moved(b, vw, hs, hs.find.query, forward, if (forward) sel_to(sel) else sel_from(sel))
  if (moved != null) {
    sel = moved.sel
    vw = moved.vw
    hs = moved.hs
    frame = request_exact(evt.target, hs, b, vw, frame)
    rev = project(vw, b, hs, sel, rev)
  }
  'prevent-default'
}
// Pass 2 (CED14v2, OQ16): the exact runs for the window swap in with one
// assignment, so no frame mixes rows from before and after the parse. A
// composing row is not re-rendered until its commit, which asks again.
on source_frame(evt) {
  if (evt.detail != frame) { return 'pass' }
  frame = 0
  if (comp != null or not needs_exact(hs, b, vw)) { return 'handled' }
  hs = exact(hs, b, vw)
  rev = project(vw, b, hs, sel, rev)
  'handled'
}
// Measure the surface as soon as it is laid out, and again whenever the
// window resizes: the rendered rows and the wrap width follow it.
on load(evt) {
  vw = mounted(vw, evt.target, rev)
  frame = request_exact(evt.target, hs, b, vw, frame)
}
on resize(evt) {
  if (vw.handle == null) { return }
  vw = follow(b, measured(vw, dom.root_node(evt.target)), sel.head)
  frame = request_exact(evt.target, hs, b, vw, frame)
  rev = project(vw, b, hs, sel, rev)
}
on closerequest(evt) {
  if (not sess.is_dirty(session, b)) {
    dom.request_window_close(evt.target)
    return
  }
  dialog = {kind: 'close'}
}
