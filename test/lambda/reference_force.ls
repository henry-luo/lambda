// The reference type, the force step `#`, address-of `&` and `===`.
// Design: vibe/Lambda_Design_Reference.md (PTH30-PTH45v2).

"===== THE reference TYPE (PTH30) =====";

// `path` is a scalar type DISJOINT from `symbol`; `reference` is the alias
// `symbol | path` (URI = URN | URL).
let p = /.etc.hosts;
type(p);
(p is path);
(p is symbol);
(p is reference);
('name' is reference);
(1 is reference);
("s" is reference);

// S6.2.1v2 band: … < symbol < path < string < …
sort([/.b, "s", 1, 'z', /.a]);

"===== THE FORCE STEP # (PTH31-PTH34) =====";

let doc = \.test.lambda.'reference_doc.json';
// A path is LAZY: `.` appends a step and reads nothing (R1).
type(doc.a.b);
// `#` forces. The fragment sugar `p#name` is `p#.name` (PTH33).
doc#title;
doc#a.b.c;
// PTH34: forcing resolves the longest document-naming prefix, so all three
// spellings are one value.
doc.a.b.c#;
doc#.a.b.c;
// Absence inside a forced document is null (S7.1.1v3).
(doc#missing == null);

"===== ADDRESS-OF & AND === (PTH40-PTH45v2) =====";

let d = doc#;
// Identity is the container's path within its document (PTH42).
&d.a;
&d.a.b;
&d.items;
// PTH41: scalars and runtime-constructed data carry NO identity.
(&d.title == null);
(&1 == null);
(&{x: 1} == null);
// A printed reference re-read forces to the head (PTH-O9).
(&d.a.b)#;
// `a === b` is `&a != null and &a == &b` (PTH45v2).
(d.a === d.a);
(doc#a === d.a);
(d.a === d.items);
// Identity-less operands compare FALSE, never an error (S1.9).
(1 === 1);
({a: 1} === {a: 1})
