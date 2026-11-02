import .js_promise_module

pn main() {
    print(wait(later(4))^)
    var value = null
    { let __error_outcome_1 = wait(rejectLater()) ^ { ^ }; if (__error_outcome_1 is error) { value = __error_outcome_1 } else match __error_outcome_1 { case any { value = ~ } } }
    print(type(value))
}
