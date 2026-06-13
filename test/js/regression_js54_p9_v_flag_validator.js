// Js54 P9 — Gate C Layer 1 regression coverage.
// Verifies the JS-side validator accepts /v patterns with nested classes,
// --/&& set operators, and \q{...} alternation. Layer 1 only fixes the
// validator; the RE2 backend may still reject the pattern. We allow
// RE2-side syntax errors but require the JS validator to NOT report
// "Annex B legacy syntax".

function expectNoAnnexBReject(pattern, flags, msg) {
    try {
        new RegExp(pattern, flags);
    } catch (e) {
        if (e.name === "SyntaxError" && /Annex B/i.test(e.message)) {
            throw new Error("assert: " + msg + " — validator rejected /" + pattern + "/" + flags + ": " + e.message);
        }
    }
}
function assertConstructs(pattern, flags, msg) {
    try {
        new RegExp(pattern, flags);
    } catch (e) {
        throw new Error("assert: " + msg + " — /" + pattern + "/" + flags + " threw: " + e.message);
    }
}
function assertRejects(pattern, flags, msg) {
    try {
        new RegExp(pattern, flags);
        throw new Error("assert: " + msg + " — expected SyntaxError for /" + pattern + "/" + flags);
    } catch (e) {
        if (e.name !== "SyntaxError") throw e;
    }
}

// /v should pass the Annex B validator for these constructs (Layer 1 acceptance).
// RE2 may still reject; that's expected — Layer 2 fixes the semantics.
expectNoAnnexBReject("[[a-z]--[aeiou]]", "v", "set difference");
expectNoAnnexBReject("[[a-z]&&[aeiou]]", "v", "set intersection");
expectNoAnnexBReject("[[abc][def]]", "v", "nested classes");
expectNoAnnexBReject("[\\q{ab|cd}]", "v", "quoted-string alternation");
expectNoAnnexBReject("[[A-Z]&&[A-F]]", "v", "intersection range");

// /u must reject all of the above as Annex B legacy syntax
assertRejects("[[a-z]--[aeiou]]", "u", "/u set difference rejected");
assertRejects("[\\q{ab}]", "u", "/u \\q rejected");

// Regular patterns still compile under /u and /v
assertConstructs("[a-z]", "u", "simple range u");
assertConstructs("[a-z]", "v", "simple range v");
assertConstructs("\\p{Lu}", "u", "property escape u");
assertConstructs("\\p{Lu}", "v", "property escape v");
assertConstructs("[\\p{Lu}]", "u", "property escape in class u");
assertConstructs("[\\p{Lu}]", "v", "property escape in class v");

console.log("regression_js54_p9_v_flag_validator: OK");
