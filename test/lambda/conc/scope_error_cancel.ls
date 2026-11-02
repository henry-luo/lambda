pn child() {
    print("late")
}
pn fail() int^ {
    start(child)
    print("before")
    raise error("boom")
}

pn main() {
    var value = null
    { let __error_outcome_1 = fail() ^ { ^ }; if (__error_outcome_1 is error) { value = __error_outcome_1 } else match __error_outcome_1 { case any { value = ~ } } }
    print(value is error)
    print(value.message)
}
