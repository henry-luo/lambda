// Escapes on the spec-backtracker route. Lambda sends a pattern to the
// backtracker when it uses a backreference, a multiline unescaped anchor, a
// nested lookaround, or a lookahead with a capture group. The backtracker used
// to have no case for \u, \c or \p, so those escapes were parsed as the literal
// letter and every pattern below silently returned the wrong answer.
function check(label, got, want) {
  console.log((got === want ? 'ok   ' : 'FAIL ') + label + ' => ' + got);
}
// \uHHHH / \u{...}
check('u-escape multiline', new RegExp("^\\u0041$", "m").test("A"), true);
check('u-escape backref', new RegExp("(a)\\1\\u0042").test("aaB"), true);
check('u-brace astral', /^\u{1F600}$/mu.test("\u{1F600}"), true);
check('u-escape in class', new RegExp("^[\\u0041-\\u005a]+$", "m").test("ABC"), true);
// surrogate pair combines to one codepoint
check('u-surrogate pair under /u', new RegExp("^\\uD83D\\uDE00$", "mu").test("\u{1F600}"), true);
// \cX control escape
check('c-escape tab', /^\cI$/m.test("\t"), true);
// \p{...} / \P{...}
check('p-property backref', /(\p{L})\1/u.test("aa"), true);
check('p-property multiline', new RegExp("^\\p{L}$", "mu").test("A"), true);
check('P-property negated', new RegExp("^\\P{L}$", "mu").test("1"), true);
// Canonicalize: non-ASCII folds, but not when its uppercase is ASCII
check('sigma folds', new RegExp("^\\u03a3$", "mi").test("σ"), true);
check('lookbehind sigma folds', /(?<=Σ+)b/i.test("σb"), true);
check('long-s does not fold to s', new RegExp("^\\u017f$", "i").test("s"), false);
check('kelvin does not fold to k', new RegExp("^\\u212a$", "i").test("k"), false);
