// uuid(): a random version 4 UUID (RFC 4122) from a secure source; a
// procedure, since each call differs.

pn main() {
    let ids = [for (i in 1 to 50) uuid()^]
    let a = ids[0]
    print(len(a), " ", slice(a, 8, 9), slice(a, 13, 14), slice(a, 18, 19), slice(a, 23, 24), " version ", slice(a, 14, 15), "\n")
    print("variant ", slice(a, 19, 20) in ["8", "9", "a", "b"], "\n")
    print("hex ", all([for (id in ids) id is \(("0" to "9" | "a" to "f" | "-"){36})]), "\n")
    print("distinct ", len(unique(ids)), "\n")
}
