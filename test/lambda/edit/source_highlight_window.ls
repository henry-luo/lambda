// Window-parse fidelity for the source highlighter
// (vibe/radiant/Radiant_Design_Source_Editor.md CED16v2): the runs of every
// line from a window parse equal the full parse's, with or without the
// restart cache, and after an edit that opens a fence far above the window.
import buf: lambda.edit.source_buffer
import syn: lambda.edit.source_highlight

// Constructs that cross blank lines (fences, HTML comments), front matter,
// setext headings, tables and lists with indented continuation.
let unit = "Intro **bold** and `code` text\nwith a second line.\n\n```js\nlet a = 1;\n\nlet b = 2;\n```\n\n<!--\nhidden\n\nstill hidden\n-->\n\nSetext title\n============\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n- item *one*\n\n    indented continuation\n\n~~~\ntilde fence\n~~~\n\n> quoted **strong**\n\n"
let text = "---\nfront: matter\n---\n\n" ++ join([for (i in 0 to 39) unit], "")
let b = buf.from_text(text)
let full = syn.highlight(b, 0, b.count - 1, syn.empty_scan(), 'markdown')

fn key(runs) => join([for (r in runs or []) string(r.s) ++ "-" ++ string(r.e) ++ r.c], ",")
fn same(h1, b1, h2, b2, first, final) =>
  len([for (l in first to final where key(syn.runs_for(h1, b1, l)) != key(syn.runs_for(h2, b2, l))) l])

let W = 30
let starts = [for (s in 0 to b.count - W where s % 7 == 0) s]

"lines, highlighted lines:";
[b.count, len([for (l in 0 to b.count - 1 where len(syn.runs_for(full.hl, b, l) or []) > 0) l])]

"windows without a cache, mismatched lines:";
sum([for (s in starts) same(syn.highlight(b, s, s + W - 1, syn.empty_scan(), 'markdown').hl, b, full.hl, b, s, s + W - 1)])

// thread the cache through the windows in order, as the editor scrolls
fn cached(b1, ss, i, scan, bad) {
  if (i >= len(ss)) bad
  else {
    let s = ss[i]
    let h = syn.highlight(b1, s, s + W - 1, scan, 'markdown')
    cached(b1, ss, i + 1, h.scan, bad + same(h.hl, b1, full.hl, b1, s, s + W - 1))
  }
}
"windows with the cache, mismatched lines:";
cached(b, starts, 0, syn.empty_scan(), 0)

// open a fence near the top: every later line becomes code, and the cache
// carried through scan_after must agree with a fresh parse of the new buffer
"after opening a fence above the window:";
let warm = syn.highlight(b, 900, 929, syn.empty_scan(), 'markdown')
let d = buf.delta(buf.loc(4, 0), buf.loc(4, 0), ["```", ""])
let a = buf.apply_delta(b, d)
let carried = syn.scan_after(warm.scan, a)
let fresh = syn.highlight(a.buf, 900, 929, syn.empty_scan(), 'markdown')
let reused = syn.highlight(a.buf, 900, 929, carried, 'markdown');
[same(fresh.hl, a.buf, reused.hl, a.buf, 900, 929), key(syn.runs_for(reused.hl, a.buf, 905))]

// reference links whose definitions lie outside the window, below it (the
// usual place) and above it, resolve as in the full parse: the labels each
// chunk defines travel with the cache, also after an edit adds a definition
"reference links defined outside the window:";
let refs = join([for (i in 0 to 299) "See [doc " ++ string(i) ++ "][d" ++ string(i % 3) ++ "] and [top].\n\n"], "") ++
           "[d0]: http://d0\n[d1]: http://d1\n"
let rtext = "[top]: http://top\n\n" ++ refs
let rb = buf.from_text(rtext)
let rfull = syn.highlight(rb, 0, rb.count - 1, syn.empty_scan(), 'markdown')
let rwin = syn.highlight(rb, 300, 329, syn.empty_scan(), 'markdown')
let radd = buf.apply_delta(rb, buf.delta(buf.loc(rb.count - 1, 0), buf.loc(rb.count - 1, 0), ["[d2]: http://d2", ""]))
let rcarried = syn.highlight(radd.buf, 300, 329, syn.scan_after(rwin.scan, radd), 'markdown')
let rafter = syn.highlight(radd.buf, 0, radd.buf.count - 1, syn.empty_scan(), 'markdown');
// line 306 cites [d2], which only the edit defines
[same(rwin.hl, rb, rfull.hl, rb, 300, 329), key(syn.runs_for(rwin.hl, rb, 302)), key(syn.runs_for(rwin.hl, rb, 306)),
 same(rcarried.hl, radd.buf, rafter.hl, radd.buf, 300, 329), key(syn.runs_for(rcarried.hl, radd.buf, 306))]
