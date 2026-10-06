// lambda.edit source highlighting (vibe/radiant/Radiant_Design_Source_Editor.md
// CED14v2, CED15, CED16v2): parser spans become per-line color runs, an edit
// maps runs provisionally, and the restart cache realigns after chunk rebuilds.
import buf: lambda.edit.source_buffer
import hl: lambda.edit.source_highlight

let md = "---\ntitle: T\n---\n# Title *here*\n\nSome **bold** and `code` with a [link](http://x.y).\n\n```js\nlet x = 1;\n```\n\n- item **b**\n- [ ] task\n\n> quote\n\n| a | b |\n|---|---|\n\n[ref]: http://r.s\n"
let b = buf.from_text(md)
let h = hl.highlight(b, 0, b.count - 1, hl.empty_scan())

fn show(runs) => [for (r in runs) string(r.s) ++ "-" ++ string(r.e) ++ ":" ++ r.c]

"runs:";
[for (l in 0 to b.count - 1) [buf.line(b, l), show(hl.runs_for(h.hl, b, l))]]

"scan:";
[h.scan.valid, len(h.scan.states)]

// typing inside **bold** grows the bold run and shifts the code run
"typing inside a token:";
let d1 = buf.delta(buf.loc(5, 9), buf.loc(5, 9), ["X"])
let a1 = buf.apply_delta(b, d1)
let m1 = hl.after_steps(h.hl, h.scan, [{delta: d1, applied: a1}], a1.buf.version);
[m1.hl.exact, show(hl.runs_for(m1.hl, a1.buf, 5))]

// a newline inside the code span splits its run across two lines
"newline split:";
let d2 = buf.delta(buf.loc(5, 20), buf.loc(5, 20), ["", ""])
let a2 = buf.apply_delta(b, d2)
let m2 = hl.after_steps(h.hl, h.scan, [{delta: d2, applied: a2}], a2.buf.version);
[show(hl.runs_for(m2.hl, a2.buf, 5)), show(hl.runs_for(m2.hl, a2.buf, 6)), show(hl.runs_for(m2.hl, a2.buf, 7))]

// a multi-line paste leaves its middle lines plain
"paste:";
let d3 = buf.delta(buf.loc(3, 2), buf.loc(3, 2), ["A", "B", "C"])
let a3 = buf.apply_delta(b, d3)
let m3 = hl.after_steps(h.hl, h.scan, [{delta: d3, applied: a3}], a3.buf.version);
[show(hl.runs_for(m3.hl, a3.buf, 3)), show(hl.runs_for(m3.hl, a3.buf, 4)), show(hl.runs_for(m3.hl, a3.buf, 5))]

// the exact parse after the edit agrees with the provisional runs for typing
"exact after typing:";
let e1 = hl.highlight(a1.buf, 0, a1.buf.count - 1, m1.scan);
[show(hl.runs_for(e1.hl, a1.buf, 5)) == show(hl.runs_for(m1.hl, a1.buf, 5))]

"scan_after:";
[hl.scan_after({states: [0,0,0, 1,96,3, 0,0,0, 0,0,0], valid: 4}, {chunk: 1, removed: 1, added: 2}),
 hl.scan_after({states: [0,0,0, 1,96,3, 0,0,0, 0,0,0], valid: 4}, {chunk: 0, removed: 2, added: 1})]
