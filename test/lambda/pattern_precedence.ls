// S11.1.2v3 (SP19): inside a pattern island the tiers are, tightest first, an
// atom (a range `"a" to "z"` is one), prefix `!`, a suffix, concatenation and
// `|`. `|` is the only binary operator (SP20): patterns intersect whole.

'1. a range is one atom'
"1.1"; ("abc" is \("a" to "c"+))
"1.2"; ("abd" is \("a" to "c"+))
"1.3"; ("a5" is \("a" to "c" "\d"))
"1.4"; ("1" is \(!"a" to "z"))
"1.5"; ("m" is \(!"a" to "z"))

'2. prefix `!` binds before a suffix'
"2.1"; ("xy" is \(!"\d"+))
"2.2"; ("x1" is \(!"\d"+))
"2.3"; ("ab" is \(!"\d"{2}))
"2.4"; ("<ab>" is \("<" !">"* ">"))
"2.5"; ("x_" is \(!("\d" | "-")+))
"2.6"; ("12" is \(!"a" to "z"+))

'3. a suffix binds before concatenation; a quoted string is one atom'
"3.1"; ("abb" is \("a" "b"+))
"3.2"; ("abab" is \("a" "b"+))
"3.3"; ("abab" is \("ab"+))
"3.4"; ("abb" is \("ab"+))
"3.5"; ("x1" is \(!"\d" "\w"))
"3.6"; ("1x" is \(!"\d" "\w"))

'4. concatenation binds before `|`'
"4.1"; ("a" is \("a" | "b" "c"))
"4.2"; ("bc" is \("a" | "b" "c"))
"4.3"; ("ac" is \("a" | "b" "c"))
"4.4"; ("cd" is \("a" "b" | "c" "d"))
"4.5"; ("ad" is \("a" "b" | "c" "d"))
"4.6"; ("bbb" is \("a" | "b"+))
"4.7"; ("5" is \("a" to "c" | "\d"))

'5. intersection and exclusion are between whole patterns'
"5.1"; ("abc" is (\("\a"+) & \("\w"+)))
"5.2"; ("ab1" is (\("\a"+) & \("\w"+)))
"5.3"; ("ab" is (\("\w"+) ! \("\d"+)))
"5.4"; ("12" is (\("\w"+) ! \("\d"+)))
