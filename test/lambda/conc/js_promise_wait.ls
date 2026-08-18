import .js_promise_module

pn main() {
    print(wait(later(4))^)
    var value = null
    wait(rejectLater()) ^ { value = ^ } ~ { value = ~ }
    print(type(value))
}
