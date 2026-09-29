// table admission and string spans preserve the shared oracle and UTF-8 text.
import ~~.~~.benchmark.hyphen_tables
import ~~.~~.benchmark.hyphen_common
import ~~.~~.benchmark.hyphen_typed

pn main() {
    let tables: HyphenTables = load_hyphen_tables()
    let cases: string[][] = hyphen_cases
    var matched: int = 0
    var index: int = 0
    while (index < len(cases)) {
        if (hyphenate(tables, cases[index][0]) == cases[index][1]) {
            matched = matched + 1
        }
        index = index + 1
    }
    print("cases:" ++ matched ++ "\n")
    print("empty:" ++ (hyphenate(tables, "") == "") ++ "\n")
    print(hyphenate(tables, "--- &amp; 123 😀 café") ++ "\n")
    print(hyphenate(tables, "<em title=\"configuration\">configuration</em>") ++ "\n")
    print(hyphenate(tables, "<article data-note=\"configuration\"") ++ "\n")
    print(hyphenate(tables, "co-operate already-hyphenated") ++ "\n")
}
