// S17.7.1 (SP22): case-insensitive matching folds by Unicode simple case
// folding, locale-independent, one code point to one, on every path. A literal
// needle folded ASCII only, so it missed what the same pattern found (§7 #8).

'1. a literal folds as a pattern does'
"1.1"; (find("École", "é", {ignore_case: true}) |> ~.value)
"1.2"; (find("École", \("é"), {ignore_case: true}) |> ~.value)
"1.3"; (find("É1", \("é" d), {ignore_case: true}) |> ~.value)
"1.4"; (find("STRASSE ẞ", "ß", {ignore_case: true}) |> ~.index)

'2. simple folding: one code point to one'
"2.1"; (find("ß", "ss", {ignore_case: true}) |> ~.value)
"2.2"; (find("K", "k", {ignore_case: true}) |> ~.value)
"2.3"; (find("ς", "σ", {ignore_case: true}) |> ~.value)
"2.4"; (find("İ", "i", {ignore_case: true}) |> ~.value)

'3. replace'
"3.1"; [replace("Éa éA", "é", "e", {ignore_case: true})]
"3.2"; [replace("ÉÉÉ", "é", "e", {ignore_case: true, limit: 2})]
"3.3"; [replace("aAa", "a", "-", {ignore_case: true, last: 1})]
"3.4"; [replace('ÉCOLE', "é", "e", {ignore_case: true})]
