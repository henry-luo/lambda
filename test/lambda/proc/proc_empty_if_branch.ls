// Empty and comment-only if/else blocks in pn should behave as no-op branches.

pn run_empty_branches() {
    var i = 0
    var sum = 0
    while (i < 5) {
        i = i + 1
        if (i == 1) {
            sum = sum + 10
        } else if (i == 2) {
        } else if (i == 3) {
        } else {
            sum = sum + 1
        }
    }
    "i=" ++ i ++ ",sum=" ++ sum
}

pn run_comment_only_branches() {
    var i = 0
    var sum = 0
    while (i < 5) {
        i = i + 1
        if (i == 1) {
            sum = sum + 10
        } else if (i == 2) {
            // marker op: intentionally no state change
        } else if (i == 3) {
            // another no-op marker
        } else {
            sum = sum + 1
        }
    }
    "i=" ++ i ++ ",sum=" ++ sum
}

pn main() {
    print("empty:" ++ run_empty_branches())
    print(" comment:" ++ run_comment_only_branches())
    "done"
}
