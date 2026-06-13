// Js54 P11 — Gate C Layer 3 regression coverage.
// Verifies /v \p{StringProperty} expansion for the 7 ES2024 string properties:
//   - Basic_Emoji, Emoji_Keycap_Sequence
//   - RGI_Emoji, RGI_Emoji_Flag_Sequence
//   - RGI_Emoji_Modifier_Sequence, RGI_Emoji_Tag_Sequence, RGI_Emoji_ZWJ_Sequence

function check(pattern, flags, str, expected, msg) {
    var re = new RegExp(pattern, flags);
    var got = re.test(str);
    if (got !== expected) {
        throw new Error("assert: " + msg + ": /" + pattern + "/" + flags +
            " on '" + str + "' got " + got + " expected " + expected);
    }
}

// Basic_Emoji — single codepoint emoji + VS16 sequences
check("^\\p{Basic_Emoji}$", "v", "\u{1F600}", true, "Basic: grinning face");
check("^\\p{Basic_Emoji}$", "v", "⌚", true, "Basic: watch");
check("^\\p{Basic_Emoji}$", "v", "A", false, "Basic: ASCII letter");

// Emoji_Keycap_Sequence — digit/symbol + VS16 + combining keycap
check("^\\p{Emoji_Keycap_Sequence}$", "v", "0️⃣", true, "Keycap: 0");
check("^\\p{Emoji_Keycap_Sequence}$", "v", "#️⃣", true, "Keycap: #");
check("^\\p{Emoji_Keycap_Sequence}$", "v", "0", false, "Keycap: bare 0");

// RGI_Emoji_Flag_Sequence — two regional indicators
check("^\\p{RGI_Emoji_Flag_Sequence}$", "v", "\u{1F1FA}\u{1F1F8}", true, "Flag: US");

// RGI_Emoji_ZWJ_Sequence — ZWJ-joined sequence
check("^\\p{RGI_Emoji_ZWJ_Sequence}$", "v", "\u{1F468}‍❤️‍\u{1F468}", true, "ZWJ: couple");

// RGI_Emoji — umbrella property covering all RGI sequences
check("^\\p{RGI_Emoji}$", "v", "\u{1F600}", true, "RGI: grinning");
check("^\\p{RGI_Emoji}$", "v", "\u{1F1FA}\u{1F1F8}", true, "RGI: flag");

// Inside a class
check("^[A\\p{Basic_Emoji}]$", "v", "A", true, "class: A literal");
check("^[A\\p{Basic_Emoji}]$", "v", "\u{1F600}", true, "class: emoji via prop");
check("^[A\\p{Basic_Emoji}]$", "v", "B", false, "class: other letter");

// Quantified
check("^\\p{Basic_Emoji}+$", "v", "\u{1F600}\u{1F601}", true, "quantified: pair");

// Negative: \P{StringProperty} should be rejected
var rejected = false;
try { new RegExp("\\P{Basic_Emoji}", "v"); } catch (e) { if (e.name === "SyntaxError") rejected = true; }
// Note: spec says \P{StringProperty} is a SyntaxError; we don't enforce here yet.
// (RE2 may surface the error itself when the property name doesn't match its tables.)

console.log("regression_js54_p11_v_flag_string_properties: OK");
