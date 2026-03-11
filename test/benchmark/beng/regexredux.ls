// BENG Benchmark: regex-redux
// Read FASTA input, count regex pattern matches, perform substitutions
// Uses file input instead of stdin (Lambda adaptation)
// Expected: matches benchmarksgame regexredux-output.txt

let INPUT_PATH = "test/benchmark/beng/input/fasta_1000.txt"

// 9 regex patterns to count matches
// Lambda uses ("a"|"b") instead of [ab] for character alternation
type pat1 = "agggtaaa" | "tttaccct"
type pat2 = ("c" | "g" | "t") "gggtaaa" | "tttaccc" ("a" | "c" | "g")
type pat3 = "a" ("a" | "c" | "t") "ggtaaa" | "tttacc" ("a" | "g" | "t") "t"
type pat4 = "ag" ("a" | "c" | "t") "gtaaa" | "tttac" ("a" | "g" | "t") "ct"
type pat5 = "agg" ("a" | "c" | "t") "taaa" | "ttta" ("a" | "g" | "t") "cct"
type pat6 = "aggg" ("a" | "c" | "g") "aaa" | "ttt" ("c" | "g" | "t") "ccct"
type pat7 = "agggt" ("c" | "g" | "t") "aa" | "tt" ("a" | "c" | "g") "taccct"
type pat8 = "agggta" ("c" | "g" | "t") "a" | "t" ("a" | "c" | "g") "ataccct"
type pat9 = "agggtaa" ("c" | "g" | "t") | ("a" | "c" | "g") "aataccct"

// pattern to match header lines
type header_pat = ">" \.*

pn main() {
    var __t0 = clock()
    let text^err = input(INPUT_PATH, 'text')
    var original_len = len(text)

    // step 1: remove FASTA headers and newlines to get bare sequence
    var seq = replace(text, header_pat, "")
    seq = replace(seq, "\n", "")
    var clean_len = len(seq)

    // step 2: count matches for each of 9 patterns
    print("agggtaaa|tttaccct " ++ string(len(find(seq, pat1))) ++ "\n")
    print("[cgt]gggtaaa|tttaccc[acg] " ++ string(len(find(seq, pat2))) ++ "\n")
    print("a[act]ggtaaa|tttacc[agt]t " ++ string(len(find(seq, pat3))) ++ "\n")
    print("ag[act]gtaaa|tttac[agt]ct " ++ string(len(find(seq, pat4))) ++ "\n")
    print("agg[act]taaa|ttta[agt]cct " ++ string(len(find(seq, pat5))) ++ "\n")
    print("aggg[acg]aaa|ttt[cgt]ccct " ++ string(len(find(seq, pat6))) ++ "\n")
    print("agggt[cgt]aa|tt[acg]taccct " ++ string(len(find(seq, pat7))) ++ "\n")
    print("agggta[cgt]a|t[acg]ataccct " ++ string(len(find(seq, pat8))) ++ "\n")
    print("agggtaa[cgt]|[acg]aataccct " ++ string(len(find(seq, pat9))) ++ "\n")

    // step 3: IUPAC code substitutions — each single letter expands to alternatives
    var result = seq
    result = replace(result, "B", "(c|g|t)")
    result = replace(result, "D", "(a|g|t)")
    result = replace(result, "H", "(a|c|t)")
    result = replace(result, "K", "(g|t)")
    result = replace(result, "M", "(a|c)")
    result = replace(result, "N", "(a|c|g|t)")
    result = replace(result, "R", "(a|g)")
    result = replace(result, "S", "(c|g)")
    result = replace(result, "V", "(a|c|g)")
    result = replace(result, "W", "(a|t)")
    result = replace(result, "Y", "(c|t)")

    // step 4: print lengths
    print("\n")
    print(string(original_len) ++ "\n")
    print(string(clean_len) ++ "\n")
    print(string(len(result)) ++ "\n")
    var __t1 = clock()
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
