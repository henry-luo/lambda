// Action C regression: a pending direct return cannot be forwarded through a
// shared epilogue when another source return owns a different lane.

pn pending_item(value) {
    return value
}

pn maybe_pending(value) {
    if (value == 0) {
        return 0
    }
    return pending_item(value)
}

pn main() {
    print(string(maybe_pending(0)) ++ "\n")
    print(string(maybe_pending(7i64)) ++ "\n")
}
