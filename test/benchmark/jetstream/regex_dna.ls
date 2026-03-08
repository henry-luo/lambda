// JetStream Benchmark: regex-dna (SunSpider)
// Regex matching and IUPAC code substitution on DNA sequences
// Original: The Computer Language Shootout (Jesse Millikan / jose fco. gonzalez)
// Tests regex find/replace on a ~300KB DNA string

// Pattern for stripping FASTA headers and newlines
string Fasta = ">" \.* "\n" | "\n"

// 9 DNA matching patterns (lowercase to match pre-lowercased DNA)
// JS originals use /ig flag for case-insensitive global matching
string Pat1 = "agggtaaa" | "tttaccct"
string Pat2 = ("c" | "g" | "t") "gggtaaa" | "tttaccc" ("a" | "c" | "g")
string Pat3 = "a" ("a" | "c" | "t") "ggtaaa" | "tttacc" ("a" | "g" | "t") "t"
string Pat4 = "ag" ("a" | "c" | "t") "gtaaa" | "tttac" ("a" | "g" | "t") "ct"
string Pat5 = "agg" ("a" | "c" | "t") "taaa" | "ttta" ("a" | "g" | "t") "cct"
string Pat6 = "aggg" ("a" | "c" | "g") "aaa" | "ttt" ("c" | "g" | "t") "ccct"
string Pat7 = "agggt" ("c" | "g" | "t") "aa" | "tt" ("a" | "c" | "g") "accct"
string Pat8 = "agggta" ("c" | "g" | "t") "a" | "t" ("a" | "c" | "g") "taccct"
string Pat9 = "agggtaa" ("c" | "g" | "t") | ("a" | "c" | "g") "ttaccct"

// Replace first occurrence only (JS string.replace with string arg replaces first match only)
pn replace_first(s, search, repl) {
    var matches = find(s, search)
    if (len(matches) == 0) {
        return s
    }
    var pos = matches[0].index
    var slen = len(search)
    return slice(s, 0, pos) ++ repl ++ slice(s, pos + slen, len(s))
}

pn main() {
    // Load DNA data from JSON (not timed)
    let data^err = input("./test/benchmark/jetstream/regex_dna_data.json", "json")
    var dna_raw = data.dna_raw
    var dna_lower = data.dna_lower
    var expected_output = data.expected_output
    var expected_dna = data.expected_dna

    var __t0 = clock()
    var pass = true
    // JetStream runs 2 iterations per runIteration
    var iter: int = 0
    while (iter < 2) {
        var dna = dna_raw

        // Strip FASTA headers and newlines
        dna = replace(dna, Fasta, "")

        // Count regex matches on lowercase DNA
        var c1 = len(find(dna_lower, Pat1))
        var c2 = len(find(dna_lower, Pat2))
        var c3 = len(find(dna_lower, Pat3))
        var c4 = len(find(dna_lower, Pat4))
        var c5 = len(find(dna_lower, Pat5))
        var c6 = len(find(dna_lower, Pat6))
        var c7 = len(find(dna_lower, Pat7))
        var c8 = len(find(dna_lower, Pat8))
        var c9 = len(find(dna_lower, Pat9))

        // Build output string for validation
        var output = ""
        output = output ++ "agggtaaa|tttaccct " ++ string(c1) ++ "\n"
        output = output ++ "[cgt]gggtaaa|tttaccc[acg] " ++ string(c2) ++ "\n"
        output = output ++ "a[act]ggtaaa|tttacc[agt]t " ++ string(c3) ++ "\n"
        output = output ++ "ag[act]gtaaa|tttac[agt]ct " ++ string(c4) ++ "\n"
        output = output ++ "agg[act]taaa|ttta[agt]cct " ++ string(c5) ++ "\n"
        output = output ++ "aggg[acg]aaa|ttt[cgt]ccct " ++ string(c6) ++ "\n"
        output = output ++ "agggt[cgt]aa|tt[acg]accct " ++ string(c7) ++ "\n"
        output = output ++ "agggta[cgt]a|t[acg]taccct " ++ string(c8) ++ "\n"
        output = output ++ "agggtaa[cgt]|[acg]ttaccct " ++ string(c9) ++ "\n"

        // Validate pattern counts
        if (output != expected_output) {
            print("FAIL: pattern output mismatch (iter " ++ string(iter) ++ ")\n")
            print("output len=" ++ string(len(output)) ++ " expected len=" ++ string(len(expected_output)) ++ "\n")
            pass = false
        }

        // Apply IUPAC substitutions on mixed-case DNA (first occurrence only)
        dna = replace_first(dna, "B", "(c|g|t)")
        dna = replace_first(dna, "D", "(a|g|t)")
        dna = replace_first(dna, "H", "(a|c|t)")
        dna = replace_first(dna, "K", "(g|t)")
        dna = replace_first(dna, "M", "(a|c)")
        dna = replace_first(dna, "N", "(a|c|g|t)")
        dna = replace_first(dna, "R", "(a|g)")
        dna = replace_first(dna, "S", "(c|t)")
        dna = replace_first(dna, "V", "(a|c|g)")
        dna = replace_first(dna, "W", "(a|t)")
        dna = replace_first(dna, "Y", "(c|t)")

        // Validate substituted DNA
        if (dna != expected_dna) {
            print("FAIL: IUPAC substitution mismatch (iter " ++ string(iter) ++ ")\n")
            print("Expected length: " ++ string(len(expected_dna)) ++ " Got length: " ++ string(len(dna)) ++ "\n")
            pass = false
        }

        iter = iter + 1
    }
    var __t1 = clock()

    if (pass) {
        print("regex-dna: PASS\n")
    } else {
        print("regex-dna: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
