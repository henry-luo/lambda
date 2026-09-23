// P0 fixture of vibe/impl/Lambda_List_Fixes.md — lives in test/lambda/ext until its
// phase turns it green, then moves to test/lambda (baseline). Golden written from the
// rulings, not from the runtime.
// S2.2.3, S2.6.2, S2.6.4: content drops null and "", merges adjacent strings
// and adjacent binaries, and keeps every other item apart; lists spread into
// content and normalize there (S2.6.3); the script top level is content
// (S16.7.2v2/S16.7.3v2). (phase P3)
// Green on both tiers after P3 (2026-09-23); moved from test/lambda/ext.

let e1 = <e "">
let e2 = <e "a" "" "b">
let e3 = <e b'\x01' b'\x02'>
let e4 = <e "a" b'\x01' "b">
let e5 = <e "a" 'x' "b">
let e6 = <e null "a" null>;
[len(content(e1)), len(content(e2)), content(e2), len(content(e3)), content(e3)[0] == b'\x0102'];
[len(content(e4)), len(content(e5)), len(content(e6)), content(e6)]
"-- lists spread into content and are normalized there --"
let e7 = <e ("a", null, "", "b")>
let e8 = <e (1, 2) "x">;
[len(content(e7)), content(e7), len(content(e8))]
"-- the top level is content: nulls drop, adjacent strings merge --";
[1]
"a"
null
"b"
