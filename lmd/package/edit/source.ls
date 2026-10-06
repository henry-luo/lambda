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
import lambda.editor.mod_doc
import lambda.editor.mod_source_pos
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
// Geometry
// ---------------------------------------------------------------------------

let LINE_H = 20.0
// rows rendered before the viewport is measured
let DEFAULT_ROWS = 48
let OVERSCAN = 2

// Rendered rows for a surface `height` pixels tall.
fn rows_for(height) => max(1, int(height / LINE_H) + OVERSCAN)

// The first window line that keeps `line` visible inside rows [0, rows - OVERSCAN).
fn follow(b, top, rows, line) {
  let visible = max(1, rows - OVERSCAN)
  let wanted = if (line < top) line else if (line >= top + visible) line - visible + 1 else top
  max(0, min(wanted, b.count - 1))
}

fn window_count(b, top, rows) => max(0, min(rows, b.count - top))

// Scrolling (wheel, track, thumb) stops when the last line reaches the bottom.
fn scrolled_top(b, rows, wanted) => max(0, min(wanted, b.count - max(1, rows - OVERSCAN)))

// Wheel motion arrives in pixels; whole lines move the window and the
// remainder carries to the next event, so slow trackpad motion still scrolls.
fn wheel_step(px) {
  let lines = int(px / LINE_H)
  {lines: lines, rest: px - float(lines) * LINE_H}
}

// ---------------------------------------------------------------------------
// Columns ↔ UTF-8 bytes (CED21): the bridge counts bytes, the model code points
// ---------------------------------------------------------------------------

fn utf8_width(ch) {
  let cp = ord(ch)
  if (cp < 128) 1 else if (cp < 2048) 2 else if (cp < 65536) 3 else 4
}

fn bytes_before(text, col) int => sum([for (ch in slice(text, 0, col)) utf8_width(ch)])

fn col_at_byte(text, offset) {
  let widths = [for (ch in text) utf8_width(ch)]
  let ends = [for (i in 0 to len(widths) - 1) sum(take(widths, i + 1))]
  let inside = [for (i in 0 to len(ends) - 1 where ends[i] > offset) i]
  if (len(inside) == 0) len(text) else inside[0]
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

fn row_leaves(b, hl, line) {
  let t = buf.line(b, line)
  let runs = syn.runs_for(hl, b, line)
  if (runs == null or len(runs) == 0) [leaf(t, null, 0)] else leaves_from(t, runs, 0, 0, [])
}

// rich_text's `view map` renders a text leaf with a `cls` as a classed span
fn window_doc(b, top, rows, hl) =>
  node('doc', [for (l in top to top + window_count(b, top, rows) - 1)
                 node('src_line', [for (lf in row_leaves(b, hl, l)) {*: text(lf.text), cls: lf.cls}])])

fn gutter(b, top, rows) =>
  <div class: "src-gutter", ["aria-hidden"]: "true",
    *[for (i in top to top + window_count(b, top, rows) - 1) <div class: "src-num", string(i + 1)>]
  >

// The scrollbar shows where the window is; a press on the track pages.
fn scrollbar(b, top, rows) {
  let span = max(1, b.count)
  let shown = min(1.0, float(rows) / float(span));
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
fn row_pos(b, hl, row, line, col) {
  let leaves = row_leaves(b, hl, line)
  let j = leaf_index(leaves, col)
  pos([row, j], bytes_before(leaves[j].text, col - leaves[j].start))
}

// A model position as a source position in the rendered window, clamped to it.
fn to_source(b, top, rows, hl, p) {
  let n = window_count(b, top, rows)
  if (n == 0 or p.line < top) pos([0, 0], 0)
  else if (p.line >= top + n) row_pos(b, hl, n - 1, top + n - 1, buf.line_len(b, top + n - 1))
  else row_pos(b, hl, p.line - top, p.line, p.col)
}

fn source_selection_of(b, top, rows, hl, sel) {
  let anchor = to_source(b, top, rows, hl, sel.anchor)
  let head = to_source(b, top, rows, hl, sel.head)
  text_selection(anchor, head)
}

// A source position from the rendered window as a model position.
fn from_source(b, top, hl, sp) {
  let path = sp.path
  if (len(path) == 0) buf.clamp(b, buf.loc(top + sp.offset, 0))
  else {
    let l = min(top + path[0], b.count - 1)
    let leaves = row_leaves(b, hl, l)
    if (len(path) == 1) buf.loc(l, if (sp.offset < len(leaves)) leaves[sp.offset].start else buf.line_len(b, l))
    else if (path[1] >= len(leaves)) buf.loc(l, buf.line_len(b, l))
    else buf.loc(l, leaves[path[1]].start + col_at_byte(leaves[path[1]].text, sp.offset))
  }
}

fn model_selection(b, top, hl, ssel) =>
  if (ssel == null or ssel.kind != 'text') null
  else {anchor: from_source(b, top, hl, ssel.anchor), head: from_source(b, top, hl, ssel.head), goal: null}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

fn caret(p) => {anchor: p, head: p, goal: null}
fn collapsed(sel) => buf.pos_cmp(sel.anchor, sel.head) == 0
fn sel_from(sel) => buf.pos_min(sel.anchor, sel.head)
fn sel_to(sel) => buf.pos_max(sel.anchor, sel.head)

// The selection an action applies to: the model's own when the DOM still
// shows its projection (it may reach past the window), else the DOM's.
fn action_selection(b, top, rows, hl, sel, evt) {
  let dom_sel = model_selection(b, top, hl, evt.source_selection)
  if (dom_sel == null) sel
  else if (source_selection_of(b, top, rows, hl, sel) == evt.source_selection) sel
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
fn navigate(b, sel, rows, evt) {
  let key = evt.key
  let extend = evt.shiftKey == true
  let primary = evt.metaKey == true or evt.ctrlKey == true
  let word = evt.altKey == true
  let page = max(1, rows - OVERSCAN - 1)
  if (key == "ArrowLeft" and evt.metaKey == true) moved(sel, buf.loc(sel.head.line, home_col(b, sel.head)), extend)
  else if (key == "ArrowRight" and evt.metaKey == true) moved(sel, buf.loc(sel.head.line, buf.line_len(b, sel.head.line)), extend)
  else if (key == "ArrowUp" and primary) moved(sel, buf.loc(0, 0), extend)
  else if (key == "ArrowDown" and primary) moved(sel, buf.doc_end(b), extend)
  else if (key == "ArrowLeft") horizontal(b, sel, false, word or evt.ctrlKey == true, extend)
  else if (key == "ArrowRight") horizontal(b, sel, true, word or evt.ctrlKey == true, extend)
  else if (key == "ArrowUp") vertical(b, sel, -1, extend)
  else if (key == "ArrowDown") vertical(b, sel, 1, extend)
  else if (key == "PageUp") vertical(b, sel, 0 - page, extend)
  else if (key == "PageDown") vertical(b, sel, page, extend)
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

// Typing one character next to the previous one extends its undo step; the
// first character may replace a selection, so typing over it undoes at once.
fn typed_char(d) => len(d.insert) == 1 and len(d.insert[0]) == 1

fn joins_last(hist, d) {
  if (len(hist.undo) == 0 or not typed_char(d) or buf.pos_cmp(d.from, d.to) != 0) false
  else {
    let prev = hist.undo[len(hist.undo) - 1]
    prev.typing and buf.pos_cmp(prev.after.head, d.from) == 0 and is_word_char(d.insert[0])
  }
}

// Apply one delta: the new buffer, selection, and history.
fn edit_step(st, d) {
  let r = buf.apply_delta(st.b, d)
  let head = buf.insert_end(d.from, d.insert)
  let after = caret(head)
  let entry = {inverses: [r.inverse], before: st.sel, after: after, typing: typed_char(d)}
  let undo = if (joins_last(st.hist, d)) {
               let prev = st.hist.undo[len(st.hist.undo) - 1];
               [*take(st.hist.undo, len(st.hist.undo) - 1),
                {*: prev, inverses: [*prev.inverses, r.inverse], after: after}]
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
    let base = if (comp == null) st else {b: rebased(comp.base, st.b), sel: comp.base_sel, hist: comp.base_hist}
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

// ---------------------------------------------------------------------------
// Highlighting (CED14v2): pass 1 maps the runs through an edit, pass 2 makes
// the window exact. Pass 2 belongs on the next frame; until the DOM layer
// offers a frame request, `settle` runs it before the handler returns.
// ---------------------------------------------------------------------------

// Rows parsed beyond the visible window, so ordinary scrolling stays inside it.
fn highlight_pad(rows) => 2 * rows

fn new_highlight(path) => {lang: syn.language_of(path), hl: null, scan: syn.empty_scan()}

// `steps` are the edits applied since the last settle ([] for none, null when
// the runs no longer describe the buffer).
fn settle(hs, steps, b, top, rows) {
  let mapped = if (steps != null and len(steps) == 0) hs
               else {*: hs, *: syn.after_steps(hs.hl, hs.scan, steps, b.version)}
  if (mapped.lang == null or syn.covers(mapped.hl, b, top, top + rows - 1)) mapped
  else {
    let pad = highlight_pad(rows)
    let r = syn.highlight(b, top - pad, top + rows + pad, mapped.scan)
    if (r == null) mapped else {*: mapped, hl: r.hl, scan: r.scan}
  }
}

// ---------------------------------------------------------------------------
// The application template
// ---------------------------------------------------------------------------

fn status_line(sel, status) {
  let where = "Ln " ++ string(sel.head.line + 1) ++ ", Col " ++ string(sel.head.col + 1)
  if (status == "") where else where ++ "  ·  " ++ status
}

// The toolbar's history buttons query this record instead of a rich editor.
fn history_probe(hist) => {kind: 'source', can_undo: len(hist.undo) > 0, can_redo: len(hist.redo) > 0}

let source_css = "
  /* the page is exactly the window: the source surface scrolls itself
     (design §6.2), so nothing below the status line may grow the body */
  body.edit-format-source { margin: 0; height: 100vh; overflow: hidden; display: flex;
                            flex-direction: column;
                            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
  .edit-format-source .edit-status { flex: none; padding: 4px 12px; font-size: 12px; color: #57606a;
                                     background: #f6f8fa; border-top: 1px solid #d0d7de; }
  .src-main { flex: 1; min-height: 0; display: flex; overflow: hidden;
              font-family: 'SF Mono', Menlo, Monaco, Consolas, monospace; font-size: 13px;
              background: #ffffff; color: #1f2328; }
  .src-gutter { flex: none; padding: 4px 10px 0 12px; text-align: right; color: #8c959f;
                background: #f6f8fa; border-right: 1px solid #d8dee4; user-select: none; }
  .src-num { height: 20px; line-height: 20px; }
  .src-text { flex: 1; min-width: 0; padding: 4px 0 0 10px; outline: none; overflow: hidden;
              white-space: pre; }
  .src-text .edit-doc > div { height: 20px; line-height: 20px; white-space: pre; }
  .src-scroll { flex: none; position: relative; width: 12px; background: #f6f8fa;
                border-left: 1px solid #d8dee4; cursor: default; }
  .src-thumb { position: absolute; left: 2px; right: 2px; min-height: 8px; border-radius: 4px;
               background: #c4c9cf; }
"

pub fn page(session, b, status) =>
  <html lang: "en",
    <head
      <meta charset: "UTF-8">
      <title sess.window_title(session, false)>
      <style files.css ++ tools.css ++ source_css ++ syn.css>
    >
    apply(<source_app session: session, buf: b, status: status,
                      hs: settle(new_highlight(session.path), [], b, 0, DEFAULT_ROWS)>, {mode: "edit"})
  >

// Bind the surface once and size the window to it: {handle, rows}.
pn mounted(surf, node, rev) {
  if (surf.handle != null) { return surf }
  let surface = dom.get_element_by_id(dom.root_node(node), "edit-surface")
  if (surface == null) { return surf }
  let r = dom.bounding_box(surface)
  {handle: dom.bind_model_edit_surface(surface, rev),
   rows: if (r != null and r.height > 0) rows_for(r.height) else surf.rows}
}

// Put the native selection where the model has it once this render lands;
// returns the model revision the request carries.
pn project(surf, b, top, hl, sel, rev) {
  let next = rev + 1
  if (surf.handle != null) {
    dom.finish_model_edit(surf.handle,
      edit_result.model_applied(false, true, false, "", source_selection_of(b, top, surf.rows, hl, sel), next))
  }
  next
}

// One keyboard edit (Tab) as a state step.
fn typed_step(st, text) => edit_step(st, buf.delta(sel_from(st.sel), sel_to(st.sel), [text]))

edit <source_app> state session: ~.session, b: ~.buf, status: ~.status, sel: caret(buf.loc(0, 0)),
                         top: 0, surf: {handle: null, rows: DEFAULT_ROWS}, hist: {undo: [], redo: []},
                         rev: 0, comp: null, hs: ~.hs, wheel_px: 0.0, drag: null, dialog: null, after_save: null {
  let dirty = sess.is_dirty(session, b);
  <body class: "edit-app edit-format-source",
    <div class: "edit-toolbar", role: "toolbar", ["aria-label"]: "Document",
      files.file_label(session, dirty)
      tools.group(tools.file_group, history_probe(hist), dirty, null)
    >
    <div class: "src-main",
      gutter(b, top, surf.rows);
      <div id: "edit-surface", class: "src-text", contenteditable: "true", spellcheck: "false",
           tabindex: "0", apply(window_doc(b, top, surf.rows, hs.hl))>
      scrollbar(b, top, surf.rows)
    >
    <div class: "edit-status", role: "status", status_line(sel, status)>
    files.dialog_view(dialog, session)
  >
}
on beforeinput(evt) {
  return 'pass'
}
on editaction(evt) {
  surf = mounted(surf, evt.target, rev)
  let input_type = evt.input_type
  // Native cut copies the DOM selection, which is clamped to the window; it
  // must delete exactly what it copied, never a longer model selection.
  let dom_sel = model_selection(b, top, hs.hl, evt.source_selection)
  let target = if (input_type == "deleteByCut" and dom_sel != null) dom_sel
               else action_selection(b, top, surf.rows, hs.hl, sel, evt)
  let history = input_type == "historyUndo" or input_type == "historyRedo"
  let composing = contains(composition_types, input_type)
  let st = {b: b, sel: target, hist: hist}
  let next = if (composing) composition_step(st, comp, input_type, evt.data)
             else if (history) history_step(st, input_type == "historyUndo")
             else {
               let d = intent_edit(b, target, input_type, evt.data)
               if (d == null) null else edit_step(st, d)
             }
  if (next == null) {
    let known = history or composing or contains(insert_types, input_type) or contains(delete_types, input_type)
    return if (known) edit_result.model_applied(false, false, false, "", source_selection_of(b, top, surf.rows, hs.hl, sel), rev)
           else edit_result.decline(true, false, "unsupported", 0)
  }
  b = next.b
  sel = next.sel
  hist = next.hist
  comp = next.comp
  top = follow(b, top, surf.rows, sel.head.line)
  hs = settle(hs, next.steps, b, top, surf.rows)
  rev = rev + 1
  status = ""
  files.sync_window(evt.target, session, b)
  edit_result.model_applied(true, true, true, "", source_selection_of(b, top, surf.rows, hs.hl, sel), rev)
}
on selectionchange(evt) {
  let picked = model_selection(b, top, hs.hl, evt.source_selection)
  // the selection the surface itself projected keeps the model's own
  if (picked != null and evt.source_selection != source_selection_of(b, top, surf.rows, hs.hl, sel)) { sel = picked }
}
on edit_cmd(req) {
  surf = mounted(surf, req.node, rev)
  let cmd = req.cmd
  if (cmd == "save") {
    let saved = files.save_now(session, b, req.node)
    session = saved.session
    status = saved.status
    if (saved.dialog != null) { dialog = saved.dialog }
  }
  else if (cmd == "save_as") { dialog = {kind: 'save_as', path: session.path} }
  else if (cmd == "undo" or cmd == "redo") {
    let next = history_step({b: b, sel: sel, hist: hist}, cmd == "undo")
    if (next != null) {
      b = next.b
      sel = next.sel
      hist = next.hist
      top = follow(b, top, surf.rows, sel.head.line)
      hs = settle(hs, next.steps, b, top, surf.rows)
      files.sync_window(req.node, session, b)
    }
    files.focus_surface(req.node)
    rev = project(surf, b, top, hs.hl, sel, rev)
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
    top = 0
    hist = {undo: [], redo: []}
    hs = settle(new_highlight(session.path), [], b, top, surf.rows)
  }
  if (dialog == null) {
    files.focus_surface(req.node)
    rev = project(surf, b, top, hs.hl, sel, rev)
  }
}
on keydown(evt) {
  surf = mounted(surf, evt.target, rev)
  let primary = evt.metaKey == true or evt.ctrlKey == true
  if (primary and lower(string(evt.key)) == "s") {
    if (evt.shiftKey == true) { dialog = {kind: 'save_as', path: session.path} }
    else {
      let saved = files.save_now(session, b, evt.target)
      session = saved.session
      status = saved.status
      if (saved.dialog != null) { dialog = saved.dialog }
    }
    return 'prevent-default'
  }
  if (dialog != null) {
    if (evt.key != "Escape") { return 'pass' }
    dialog = null
    after_save = null
    return 'prevent-default'
  }
  // a click places the caret without a selectionchange; adopt it first
  sel = action_selection(b, top, surf.rows, hs.hl, sel, evt)
  let st = {b: b, sel: sel, hist: hist}
  let next = if (primary and lower(string(evt.key)) == "z") history_step(st, evt.shiftKey != true)
             else if (evt.key == "Tab" and evt.shiftKey != true) typed_step(st, indent_unit(b))
             else null
  let moved_sel = if (next == null) navigate(b, sel, surf.rows, evt) else null
  if (next == null and moved_sel == null) {
    // an undo with nothing to undo is still this surface's key
    return if (primary and lower(string(evt.key)) == "z") 'prevent-default' else 'pass'
  }
  if (next != null) {
    b = next.b
    sel = next.sel
    hist = next.hist
    files.sync_window(evt.target, session, b)
  }
  else { sel = moved_sel }
  top = follow(b, top, surf.rows, sel.head.line)
  hs = settle(hs, if (next == null) [] else next.steps, b, top, surf.rows)
  rev = project(surf, b, top, hs.hl, sel, rev)
  'prevent-default'
}
// The editor owns its scroll position (CED13): the wheel moves the window by
// lines, and the selection is projected again because rows changed under it.
on wheel(evt) {
  if (dialog != null) { return 'pass' }
  surf = mounted(surf, evt.target, rev)
  let step = wheel_step(wheel_px + float(evt.deltaY or 0))
  wheel_px = step.rest
  let next_top = scrolled_top(b, surf.rows, top + step.lines)
  if (next_top != top) {
    top = next_top
    hs = settle(hs, [], b, top, surf.rows)
    rev = project(surf, b, top, hs.hl, sel, rev)
  }
  'prevent-default'
}
// A press on the thumb starts a drag; a press on the track pages toward it.
on mousedown(evt) {
  if (dom.node_type(evt.target) != 1) { return 'pass' }
  let on_thumb = dom.matches(evt.target, ".src-thumb")
  if (not on_thumb and not dom.matches(evt.target, ".src-scroll")) { return 'pass' }
  let track = if (on_thumb) dom.parent_node(evt.target) else evt.target
  let r = dom.bounding_box(track)
  if (on_thumb) { drag = {y: evt.y, top: top, height: r.height} }
  else {
    let thumb_y = r.top + r.height * float(top) / float(max(1, b.count))
    let page = max(1, surf.rows - OVERSCAN - 1)
    top = scrolled_top(b, surf.rows, if (evt.y < thumb_y) top - page else top + page)
    hs = settle(hs, [], b, top, surf.rows)
  }
  'prevent-default'
}
// The thumb follows the pointer: its travel over the track is the document's.
on mousemove(evt) {
  if (drag == null) { return 'pass' }
  let lines = int((evt.y - drag.y) * float(b.count) / max(1.0, drag.height))
  let next_top = scrolled_top(b, surf.rows, drag.top + lines)
  if (next_top != top) {
    top = next_top
    hs = settle(hs, [], b, top, surf.rows)
  }
  'prevent-default'
}
on mouseup(evt) {
  if (drag == null) { return 'pass' }
  drag = null
  rev = project(surf, b, top, hs.hl, sel, rev)
  'prevent-default'
}
on closerequest(evt) {
  if (not sess.is_dirty(session, b)) {
    dom.request_window_close(evt.target)
    return
  }
  dialog = {kind: 'close'}
}
