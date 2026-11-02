pn maybe_fail(flag) int^ {
    sleep(1)^
    if (flag) {
        raise error("async-handler")
    }
    return 21
}

pn main() {
    var result = null
    { let __error_outcome_1 = maybe_fail(false) ^ { ^ }; if (__error_outcome_1 is error) { result = "unexpected-error" } else match __error_outcome_1 { case any { result = ~ } } }
    print(result)
    { let __error_outcome_2 = maybe_fail(true) ^ { ^ }; if (__error_outcome_2 is error) { result = __error_outcome_2.message } else match __error_outcome_2 { case any { result = "unexpected-value" } } }
    print(result)
}
