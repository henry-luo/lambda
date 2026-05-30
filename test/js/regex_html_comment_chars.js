// Js52 P2: regex literals containing `<!--` or `-->` byte sequences.
// Before Js52, js_normalize_source_for_parser rewrote `<!--` to `//--`
// even inside regex literals, breaking markdown-it / parser-style code
// (e.g. `[ /^<!--/, /-->/, true ]`).

// === bare /-->/
var r1 = /-->/;
console.log(r1.source);
console.log(r1.test('foo-->bar'));

// === bare /<!--/
var r2 = /<!--/;
console.log(r2.source);
console.log(r2.test('foo<!--bar'));

// === both inside an array literal (markdown-it's HTML_SEQUENCES shape)
var seq = [/^<!--/, /-->/, true];
console.log(seq.length);
console.log(seq[0].source);
console.log(seq[1].source);
console.log(seq[2]);

// === inside char class
var r3 = /[<>]<!-->/;
console.log(r3.source);

// === regex with both <!-- and --> in body
var r4 = /<!--.*-->/;
console.log(r4.source);
console.log(r4.test('a<!--mid-->z'));

// === control: string with <!--  (must still work — fast path may rewrite)
var s = "<!--still-a-string-->";
console.log(s);

// === control: Annex B HTML-style script comment still consumed
var x = 1;
<!-- legacy script comment
console.log('x:', x);
