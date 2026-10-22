// An imported named pattern is the TypePattern its declaring module compiled
// and published in its own type list. Both tiers read it through the
// importer's list instead, so `p.word` was another pattern or none
// ("const_pattern: invalid index") and find/is/replace came back empty.
import p: .mod_patterns

type local = \("x"+)
fn here(s) => [for (m in find(s, p.word)) m.value];
p.words("ab 12 cd")
here("ab 12 cd")
let pairs = [for (m in find("a=1 b=22 c", p.pair)) m.value]
pairs
let w = p.word
"abc" is w
"ab1" is w
replace("ab 12 cd", p.word, "_")
split("ab 12 cd", p.word)
let xs = [for (m in find("x xx", local)) m.value]
xs
p.gaps("a  b c")
