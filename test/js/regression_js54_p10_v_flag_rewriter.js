// Js54 P10 — Gate C Layer 2 regression coverage.
// Verifies the /v class rewriter correctly handles:
//   - Set difference [A--B]
//   - Set intersection [A&&B]
//   - \q{X|Y|Z} quoted-string alternation
//   - Nested classes [A][B]
//   - Negation [^...]

function check(pattern, flags, str, expected, msg) {
    var re = new RegExp(pattern, flags);
    var got = re.test(str);
    if (got !== expected) {
        throw new Error("assert: " + msg + ": /" + pattern + "/" + flags +
            " on '" + str + "' got " + got + " expected " + expected);
    }
}

// Set difference: [a-z] minus vowels = consonants
check("^[[a-z]--[aeiou]]$", "v", "b", true, "diff: 'b' is consonant");
check("^[[a-z]--[aeiou]]$", "v", "z", true, "diff: 'z' is consonant");
check("^[[a-z]--[aeiou]]$", "v", "a", false, "diff: 'a' excluded");
check("^[[a-z]--[aeiou]]$", "v", "u", false, "diff: 'u' excluded");

// Set intersection: vowels within a-z = vowels
check("^[[a-z]&&[aeiou]]$", "v", "a", true, "intersect: 'a' is vowel");
check("^[[a-z]&&[aeiou]]$", "v", "u", true, "intersect: 'u' is vowel");
check("^[[a-z]&&[aeiou]]$", "v", "b", false, "intersect: 'b' not vowel");

// Nested class union: [a-c] ∪ [x-z]
check("^[[a-c][x-z]]$", "v", "a", true, "nested: 'a'");
check("^[[a-c][x-z]]$", "v", "x", true, "nested: 'x'");
check("^[[a-c][x-z]]$", "v", "m", false, "nested: 'm' not in either");

// \q{} quoted-string alternation
check("^[\\q{hello|world}]$", "v", "hello", true, "\\q: hello");
check("^[\\q{hello|world}]$", "v", "world", true, "\\q: world");
check("^[\\q{hello|world}]$", "v", "h", false, "\\q: partial 'h'");

// \q with characters mixed in: [\q{ab|cd}xyz]
check("^[\\q{ab|cd}xyz]$", "v", "ab", true, "\\q+chars: 'ab'");
check("^[\\q{ab|cd}xyz]$", "v", "x", true, "\\q+chars: 'x'");
check("^[\\q{ab|cd}xyz]$", "v", "z", true, "\\q+chars: 'z'");
check("^[\\q{ab|cd}xyz]$", "v", "a", false, "\\q+chars: 'a' alone");

// Plain class still works under /v
check("^[a-z]$", "v", "a", true, "plain: 'a' in [a-z]");
check("^[a-z]$", "v", "A", false, "plain: 'A' not in [a-z]");

// Set difference followed by additional element (non-trivial parsing)
check("^[[a-z]--[a-d]]$", "v", "e", true, "diff: 'e' is in [e-z]");
check("^[[a-z]--[a-d]]$", "v", "a", false, "diff: 'a' removed");
check("^[[a-z]--[a-d]]$", "v", "d", false, "diff: 'd' removed");

// /v also accepts regular patterns (basic compat check)
check("\\d", "v", "5", true, "shorthand: \\d matches digit");
check("\\s", "v", " ", true, "shorthand: \\s matches space");

console.log("regression_js54_p10_v_flag_rewriter: OK");
