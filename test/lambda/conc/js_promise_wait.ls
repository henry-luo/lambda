import .js_promise_module

pn main() {
    print(wait(later(4))^)
    let value = wait(rejectLater()) ^ { ^ }
    print(type(value))
}
