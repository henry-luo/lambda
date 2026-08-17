// ER-S4: equality-depth exhaustion is a static C14 fault in a local pn handler.
fn deep(n) => if (n == 0) [] else [deep(n - 1)]

pn main() {
    var fault = null
    let value = (deep(300) == deep(300)) ^ { fault = ^; null }
    print([value, fault is error, fault.code, fault.message])
}
