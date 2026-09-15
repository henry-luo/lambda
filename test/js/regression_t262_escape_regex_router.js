// Three root causes behind test262 regressions after the string-libify and
// regex-router merges.
//
// 1. String literals: `\a` is a NonEscapeCharacter in JavaScript and decodes
//    to "a"; the shared C escape decoder had mapped it to BEL (U+0007).
console.log(JSON.stringify(["\a", "\c", "\d", "\z", "\q\w", "<\/p>"]));
console.log("a" === "\a", "\n\n\abc".length, /[a-c\d]+/.exec("\n\n\abc324234\n").index);

// 2. Lazy quantifier over a group: RepeatMatcher keeps the incoming captures
//    when the continuation is tried, so the last iteration's capture survives.
console.log(JSON.stringify(/(x?)*?y/.exec("xxy")));
console.log(JSON.stringify(/(a|)*?b/.exec("aab")));
console.log(JSON.stringify(/(x?){2,}?y/.exec("xxy")));
console.log(JSON.stringify(/<body.*>((.*\n?)*?)<\/body>/i.exec(
  "<html>\n<body>\n<p>one</p>\n<p>two</p>\n</body>\n</html>")));
console.log(JSON.stringify(/(x?)*y/.exec("xxy")), JSON.stringify(/(?:x?)*?y/.exec("xxy")));

// 3. Quantifier bounds beyond int range are valid syntax: they route to the
//    backtracker (not a routing RangeError) and saturate like V8's infinity.
var max = Number.MAX_SAFE_INTEGER;
console.log(new RegExp("b{" + max + "}", "u").test(""),
  new RegExp("b{" + max + ",}?").test("a"),
  new RegExp("b{" + max + "," + max + "}").test("b"),
  new RegExp("a{0," + max + "}").exec("aaa")[0]);
