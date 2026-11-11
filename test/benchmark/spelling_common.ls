// Word's installed English checker over a fixed, independently annotated document.
import spelling: lambda.doc.spelling

pub pn run_spelling_benchmark() {
    let checker = spelling.with_words(spelling.load()^, ["Lambda", "Radiant"])
    let text = input("test/benchmark/text/spelling_document.txt", 'text')^
    let expected = input("test/benchmark/text/spelling_expected.json", 'json')^
    let t0 = clock()
    let result = spelling.check(checker, text)^
    let elapsed = (clock() - t0) * 1000.0
    if ((result.issues == expected and result.checked == 7110 and result.skipped == 0)^) {
        print("spelling: CHECKED:7110 ISSUES:720\n")
    } else {
        print("spelling: FAIL checked=" ++ string(result.checked) ++ " issues=" ++ string(len(result.issues)) ++ "\n")
    }
    print("__TIMING__:" ++ string(elapsed) ++ "\n")
}
