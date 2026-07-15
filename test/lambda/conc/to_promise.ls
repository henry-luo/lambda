import .js_promise_module

pn child() {
    sleep(1)^
    return 9
}

pn main() {
    let handle = start child()
    print(wait(toPromise(handle))^)
}
