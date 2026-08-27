// S16.10.2: data names admit keywords — map keys, element tags, attribute
// names. S16.10.3: member steps after `.` admit them too. Definition and use
// are both sigil-guarded, so no keyword construct can begin there.
let m = {type: 1, in: 2, if: 3, order: 4}
m.type + m.in + m.if + m.order

// a keyword element tag is legal (the bare-tag rejection was implementation,
// not a ruling); markup must round-trip words like HTML's real <var>.
let e = <if a: 1, "x">
e.a

// keyword attribute names, including base-type words.
let d = <div if: 1, int: 2, "t">
d.if + d.int

// the quoted-symbol spelling remains available (S15.1) and means the same tag.
let q = <'if' b: 5, "y">
q.b

// subscripts are expression space, not name space: a string key, not a keyword.
m["type"]
