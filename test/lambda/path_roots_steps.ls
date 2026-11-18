// S2.4.1v3 / S2.4.2v6: escaped addresses capture typed path steps, and every
// step is `.part` or `[key]`. `\.1` and `/.1` are paths whose first key is
// IntKey(1); `\.file.localhost[1]` is `(\.file.localhost)[1]`, the same path, because a subscript on a path
// is the dotted step, and inside a path literal every step is a key step:
// `\[1].name` is `\.1.name`, never the `name` property. `\.1` used to evaluate
// to `file./`, and `\.a[1]` read the target's content instead of `\.a.1`.
let k = 1;
let s = 'x';
let mixed = if (k > 0) 2 else 'y';
let p = \.file.localhost.a;
[
  \, \.file.localhost,
  \.1, \.1.x, \.1.2,
  \.file.localhost.1, \.file.localhost.1.2, \.file.localhost.1.x,
  \.file.localhost[1], \[1], \[1].x, \[1][2], \[1].name, \.file.localhost[1].name,
  \.a[1], \.file.localhost.a[k], \.a[s], \.a[mixed], \.a['name'], \.a[1].~~, \[1].*,
  p[1], p['q'],
  \.a[-1], \.a[1.5], \.a[1.0],
  (\.file.localhost)[1] == \.file.localhost.1, \[1] == \.1, \.file.localhost[1] == \.file.localhost.1, p[1] == \.file.localhost.a.1, \[1].name == \.1.name,
  \.a.~~, \.a.~~ == \,
  \.file.localhost.1, \./.1, \.~~.1, \.a.*.1, \.a.**.1,
  \.file.localhost.a, \ .a, \. a
]
