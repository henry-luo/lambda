// Existing repairs: S1.6, S11.1.2v4 and S10.2.4.
import d: .mod_ledger_defaults
import p: .mod_patterns
fn h(a, b = a, c = 0) => [a, b, c]
let word = p.word
let bytes = [[200u8, 100u8], [200u8, 100u8]];
[h(2) == [2, 2, 0], h(1, c: 5) == [1, 1, 5], d.h(1) == [1, 42],
 p.words("ab 12 cd") == ["ab", "cd"], ("abc" is word),
 sum(bytes, 0) == [400, 200], avg(bytes, 0) == [200, 100]]
