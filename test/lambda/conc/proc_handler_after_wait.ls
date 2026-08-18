pn maybe_fail(flag) int^ {
    sleep(1)
    if (flag) {
        raise error("async-handler")
    }
    return 21
}

pn main() {
    var result = null
    maybe_fail(false) ^ { result = "unexpected-error" } ~ { result = ~ }
    print(result)
    maybe_fail(true) ^ { result = ^.message } ~ { result = "unexpected-value" }
    print(result)
}
