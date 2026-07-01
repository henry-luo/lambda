// JetStream Benchmark: regex-dna (SunSpider)
// Regex matching and IUPAC code substitution on DNA sequences
// Original: The Computer Language Shootout (Jesse Millikan / jose fco. gonzalez)
// Tests regex find/replace on a ~300KB DNA string

// Pattern for stripping FASTA headers and newlines
type Fasta = ">" \.* "\n" | "\n"

// 9 DNA matching patterns
// JS originals use /ig flag for case-insensitive global matching
type Pat1 = "agggtaaa" | "tttaccct"
type Pat2 = ("c" | "g" | "t") "gggtaaa" | "tttaccc" ("a" | "c" | "g")
type Pat3 = "a" ("a" | "c" | "t") "ggtaaa" | "tttacc" ("a" | "g" | "t") "t"
type Pat4 = "ag" ("a" | "c" | "t") "gtaaa" | "tttac" ("a" | "g" | "t") "ct"
type Pat5 = "agg" ("a" | "c" | "t") "taaa" | "ttta" ("a" | "g" | "t") "cct"
type Pat6 = "aggg" ("a" | "c" | "g") "aaa" | "ttt" ("c" | "g" | "t") "ccct"
type Pat7 = "agggt" ("c" | "g" | "t") "aa" | "tt" ("a" | "c" | "g") "accct"
type Pat8 = "agggta" ("c" | "g" | "t") "a" | "t" ("a" | "c" | "g") "taccct"
type Pat9 = "agggtaa" ("c" | "g" | "t") | ("a" | "c" | "g") "ttaccct"

pn main() {
    // Load DNA data from JSON (not timed)
    let data^err = input("./test/benchmark/jetstream/regex_dna_data.json", "json")
    var dna_raw = data.dna_raw
    var expected_output = data.expected_output
    var expected_dna = data.expected_dna
    let match_options = {ignore_case: true}
    let first_only = {limit: 1}

    var __t0 = clock()
    var pass = true
    // JetStream runs 2 iterations per runIteration
    var iter: int = 0
    while (iter < 2) {
        var dna = dna_raw

        // Strip FASTA headers and newlines
        dna = replace(dna, Fasta, "")

        // Count regex matches using the JS benchmark's /ig behavior
        var c1 = len(find(dna, Pat1, match_options))
        var c2 = len(find(dna, Pat2, match_options))
        var c3 = len(find(dna, Pat3, match_options))
        var c4 = len(find(dna, Pat4, match_options))
        var c5 = len(find(dna, Pat5, match_options))
        var c6 = len(find(dna, Pat6, match_options))
        var c7 = len(find(dna, Pat7, match_options))
        var c8 = len(find(dna, Pat8, match_options))
        var c9 = len(find(dna, Pat9, match_options))

        // Build output string for validation
        var output = ""
        output = output ++ "agggtaaa|tttaccct " ++ c1 ++ "\n"
        output = output ++ "[cgt]gggtaaa|tttaccc[acg] " ++ c2 ++ "\n"
        output = output ++ "a[act]ggtaaa|tttacc[agt]t " ++ c3 ++ "\n"
        output = output ++ "ag[act]gtaaa|tttac[agt]ct " ++ c4 ++ "\n"
        output = output ++ "agg[act]taaa|ttta[agt]cct " ++ c5 ++ "\n"
        output = output ++ "aggg[acg]aaa|ttt[cgt]ccct " ++ c6 ++ "\n"
        output = output ++ "agggt[cgt]aa|tt[acg]accct " ++ c7 ++ "\n"
        output = output ++ "agggta[cgt]a|t[acg]taccct " ++ c8 ++ "\n"
        output = output ++ "agggtaa[cgt]|[acg]ttaccct " ++ c9 ++ "\n"

        // Validate pattern counts
        if (output != expected_output) {
            print("FAIL: pattern output mismatch (iter " ++ iter ++ ")\n")
            print("output len=" ++ len(output) ++ " expected len=" ++ len(expected_output) ++ "\n")
            pass = false
        }

        // Apply IUPAC substitutions on mixed-case DNA (JS string.replace replaces the first string match)
        dna = replace(dna, "B", "(c|g|t)", first_only)
        dna = replace(dna, "D", "(a|g|t)", first_only)
        dna = replace(dna, "H", "(a|c|t)", first_only)
        dna = replace(dna, "K", "(g|t)", first_only)
        dna = replace(dna, "M", "(a|c)", first_only)
        dna = replace(dna, "N", "(a|c|g|t)", first_only)
        dna = replace(dna, "R", "(a|g)", first_only)
        dna = replace(dna, "S", "(c|t)", first_only)
        dna = replace(dna, "V", "(a|c|g)", first_only)
        dna = replace(dna, "W", "(a|t)", first_only)
        dna = replace(dna, "Y", "(c|t)", first_only)

        // Validate substituted DNA
        if (dna != expected_dna) {
            print("FAIL: IUPAC substitution mismatch (iter " ++ iter ++ ")\n")
            print("Expected length: " ++ len(expected_dna) ++ " Got length: " ++ len(dna) ++ "\n")
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
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
