// Js54 P12 — Gate C admission regression coverage.
// Smoke tests for /v flag behaviors that should now pass via Layer 1+2.
// String-property cases (P11) are not yet covered.

function check(pattern, flags, str, expected, msg) {
    var re = new RegExp(pattern, flags);
    var got = re.test(str);
    if (got !== expected) {
        throw new Error("assert: " + msg + ": /" + pattern + "/" + flags +
            " on '" + str + "' got " + got + " expected " + expected);
    }
}

// Basic /v patterns
check("[a-z]", "v", "x", true, "/v simple class");
check("[^abc]", "v", "x", true, "/v negated class");
check("\\d+", "v", "123", true, "/v shorthand");

// Set difference and intersection
check("^[[A-Z]--[AEIOU]]+$", "v", "BCDFG", true, "/v capital consonants");
check("^[[A-Z]&&[AEIOU]]+$", "v", "AEIOU", true, "/v capital vowels");

// Nested classes (union)
check("^[[a-c][x-z]]+$", "v", "abcxyz", true, "/v nested union");

// \q{} multi-string alternation
check("^[\\q{ab|cd|ef}]+$", "v", "abcdef", true, "/v \\q sequence");

// Combinations of plain chars with \q
check("^[\\q{ab}xy]+$", "v", "abxy", true, "/v \\q with chars");

console.log("regression_js54_p12_v_flag_admission: OK");
