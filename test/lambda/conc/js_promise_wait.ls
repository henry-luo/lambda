import .js_promise_module

pn main() {
    print(wait(later(4))^)
    let value^err = wait(rejectLater())
    print(type(err))
}
