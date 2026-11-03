pn child() {
    print("late")
}

pn main() {
    let handle = start(child)
    cancel(handle)
    var value = null
    { let __error_outcome_1 = wait(handle) ^ { ^ }; if (__error_outcome_1 is error) { value = __error_outcome_1 } else match __error_outcome_1 { case any { value = ~ } } }
    print(value is error)
    print(value.message)
}
