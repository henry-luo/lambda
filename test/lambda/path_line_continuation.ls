// S16.1.1: a line break in a path means what a space there means. A trailing
// `.` is incomplete, so the step on the next line continues it (S16.2.1); a
// line-start `.step` continues a path when no statement can start with it
// (S16.2.4v3): names, quoted symbols, `*`, `**`, `~~`, and `/`. Only `.digit`
// and `[` stay ambiguous at a line start, and those are syntax errors.
let a = \.
  x;
let b = \
  .x;
let c = \.x.y
  .~~;
let d = \.x
  ./;
let e = \.x
  .**;
let f = \.x
  .'q';
let g = \.
  1.2;
let h = /.
  1;
let i = \[1]
  .name;
let j = http
  .host.x;
[a, b, c, d, e, f, g, h, i, j]
