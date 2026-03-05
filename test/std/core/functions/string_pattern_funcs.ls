// Test: String Pattern Functions
// Layer: 2 | Category: function | Covers: find, replace, split with patterns

// ===== find with string =====
find("hello world", "world")
find("hello", "xyz")
find("abcabc", "abc")

// ===== replace with string =====
replace("hello world", "world", "Lambda")
replace("aabbcc", "bb", "XX")

// ===== split with string =====
split("a,b,c", ",")
split("hello world", " ")
split("one::two::three", "::")

// ===== Pattern-aware find =====
string digits = \d+
find("abc123def", digits)
find("no digits here", digits)

// ===== Pattern-aware replace =====
replace("abc123def456", digits, "NUM")

// ===== Pattern-aware split =====
string sep = \s+
split("hello   world   test", sep)

// ===== Multiple matches =====
string word_pat = \w+
find("hello world", word_pat)
