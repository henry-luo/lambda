// S7.9.3: membership keeps the first operand error's payload.
pn main() {
    let first = error("first")
    let second = error("second")
    print((first in [1]) ^ { ^.message })
    print(" " ++ (("key" at second) ^ { ^.message }))
    print(" " ++ ((0 of second) ^ { ^.message }))
    print(" " ++ ((first in second) ^ { ^.message }) ++ "\n")
}
