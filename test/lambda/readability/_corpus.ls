// Readability.js conformance over Mozilla's test-pages corpus, run by hand
// (vibe/Lambda_Pkg_Readability.md §7); the `_` prefix keeps it out of the
// golden harness, since the corpus lives in the git-ignored ref/.
//   [READABILITY_PAGES=<test-pages dir>] [READABILITY_PAGE=<name>] \
//       ./lambda.exe run test/lambda/readability/_corpus.ls
import cmp: .mod_compare

let DEFAULT_DIR = "ref/readability/source/test/test-pages"

pn main() {
    let env = sys.proc.self.env
    let dir = env.READABILITY_PAGES or DEFAULT_DIR
    let only = env.READABILITY_PAGE
    let pages = [for (p in split(trim(cmd("ls", [dir])^), "\n") where only == null or p == only) p]
    var passed = 0
    var content_ok = 0
    var total_seconds = 0.0
    for page in pages {
        let started = clock()
        let r = cmp.check(dir, page) ^ { {page: page, ok: false, content: false, problems: ["error: " ++ string(^.message)]} }
        let seconds = clock() - started
        if (r.ok) { passed = passed + 1 }
        if (r.content) { content_ok = content_ok + 1 }
        total_seconds = total_seconds + seconds
        let mark = if (r.ok) "ok  " else "FAIL"
        print(mark ++ " " ++ page ++ " (" ++ string(round(seconds * 100) / 100) ++ "s)" ++
            join([for (p in r.problems) "\n    " ++ p], "") ++ "\n")
    }
    print("\npassed " ++ string(passed) ++ "/" ++ string(len(pages)) ++ ", content " ++ string(content_ok) ++
        "/" ++ string(len(pages)) ++ ", check time " ++ string(round(total_seconds)) ++ "s\n")
}
