// S7.7.4v2: an admitted error is captured, even in a fallible procedure.
pn captured() any^ {
    var outcome: any^ = 0
    outcome = error("retained")
    print("after assignment")
    return {captured: outcome is error, message: outcome.message}
}
pn main() {
    let result: any^ = captured();
    result
}
