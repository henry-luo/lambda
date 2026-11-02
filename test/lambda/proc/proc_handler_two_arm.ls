// S7.6.1v3: directly raised pn calls branch without an intermediate binding.
pn pn_func(value, fail) int^ {
    if (fail) {
        raise error("pn failure")
    }
    return value
}

pn main() {
    { let __error_outcome_1 = pn_func(7, false) ^ { ^ }; if (__error_outcome_1 is error) {
        print(["error", __error_outcome_1.message])
    } else match __error_outcome_1 { case any {
        print(["value", ~ * 2])
    } } }

    { let __error_outcome_2 = pn_func(7, true) ^ { ^ }; if (__error_outcome_2 is error) {
        print(["error", __error_outcome_2.message])
    } else match __error_outcome_2 { case any {
        print(["value", ~ * 2])
    } } }
}
