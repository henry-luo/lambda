// String range values use single Unicode codepoints as inclusive bounds.
'===== STRING RANGE TESTS ====='

let letters = "a" to "e"

'Test 1: Materialization'
1; len(letters)
2; letters[0]
3; letters[4]
4; for c in letters { c }

'Test 2: Membership and range matching'
5; "c" in letters
6; "cc" in letters
7; "m" is ("a" to "z")
8; "aa" is ("a" to "z")

fn classify(value) => match value {
    case "a" to "z": "letter"
    default: "other"
}

9; classify("m")
10; classify("mm")

'Test 3: Unicode codepoint bounds'
let greek = "α" to "γ"
11; [greek[0], greek[1], greek[2]]
12; "β" in greek

'Test 4: Refuse invalid bounds'
13; "ab" to "z"
14; 1 to "z"
