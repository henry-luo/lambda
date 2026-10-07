// HTML source highlighting (vibe/radiant/Radiant_Design_Source_Editor.md
// CED15v2, CED16v3, CED18): the tokenizer's lexical spans become runs, and a
// window parse agrees with a full parse, with or without the restart cache.
import buf: lambda.edit.source_buffer
import syn: lambda.edit.source_highlight

fn show(runs) => [for (r in runs or []) string(r.s) ++ "-" ++ string(r.e) ++ ":" ++ r.c]

let page = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <title>A &amp; B</title>\n  <style>\n    p { color: red; }\n  </style>\n</head>\n<body class='main' data-x=1 hidden>\n  <!-- a\n  comment -->\n  <p>Hi &copy; <b>there</b></p>\n  <script>if (a < b) x();</script>\n</body>\n</html>\n"
let pb = buf.from_text(page)
let ph = syn.highlight(pb, 0, pb.count - 1, syn.empty_scan(), 'html')

"runs:";
[for (l in 0 to pb.count - 1) [buf.line(pb, l), show(syn.runs_for(ph.hl, pb, l))]]

// constructs that cross line ends: comments, script and style bodies, tags
// with attributes on several lines
let unit = "<section id=\"s\">\n  <!-- note\n\n  more -->\n  <script>\n    let a = '<b>';\n\n    if (a < 2) {}\n  </script>\n  <a href=\"x\"\n     title='multi\nline'>link</a>\n  <style>p { color: red; }</style>\n  <p>text &amp; more</p>\n</section>\n"
let b = buf.from_text(join([for (i in 0 to 39) unit], ""))
let full = syn.highlight(b, 0, b.count - 1, syn.empty_scan(), 'html')

fn key(runs) => join([for (r in runs or []) string(r.s) ++ "-" ++ string(r.e) ++ r.c], ",")
fn same(h1, b1, h2, b2, first, final) =>
  len([for (l in first to final where key(syn.runs_for(h1, b1, l)) != key(syn.runs_for(h2, b2, l))) l])

let W = 30
let starts = [for (s in 0 to b.count - W where s % 7 == 0) s]

"lines, highlighted lines:";
[b.count, len([for (l in 0 to b.count - 1 where len(syn.runs_for(full.hl, b, l) or []) > 0) l])]

"windows without a cache, mismatched lines:";
sum([for (s in starts) same(syn.highlight(b, s, s + W - 1, syn.empty_scan(), 'html').hl, b, full.hl, b, s, s + W - 1)])

fn cached(ss, i, scan, bad) {
  if (i >= len(ss)) bad
  else {
    let s = ss[i]
    let h = syn.highlight(b, s, s + W - 1, scan, 'html')
    cached(ss, i + 1, h.scan, bad + same(h.hl, b, full.hl, b, s, s + W - 1))
  }
}
"windows with the cache, mismatched lines:";
cached(starts, 0, syn.empty_scan(), 0)

// open a comment near the top: everything after it is comment text, and the
// cache carried through scan_after agrees with a fresh parse
"after opening a comment above the window:";
let warm = syn.highlight(b, 400, 429, syn.empty_scan(), 'html')
let d = buf.delta(buf.loc(1, 0), buf.loc(1, 0), ["<!--", ""])
let a = buf.apply_delta(b, d)
let fresh = syn.highlight(a.buf, 400, 429, syn.empty_scan(), 'html')
let reused = syn.highlight(a.buf, 400, 429, syn.scan_after(warm.scan, a), 'html');
[same(fresh.hl, a.buf, reused.hl, a.buf, 400, 429), key(syn.runs_for(reused.hl, a.buf, 405))]
