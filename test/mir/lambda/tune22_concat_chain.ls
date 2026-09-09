// T22-6: a left-associated all-string append chain remains on the one-owner
// builder. A non-string suffix stays on generic join lowering.
pn concat_chain_tune22() string {
    var result: string = ""
    var a: string = "a"
    var b: string = "b"
    var c: string = "c"
    result = result ++ a ++ b ++ c
    return result
}

pn concat_mixed_tune22() string {
    var result: string = ""
    result = result ++ "x" ++ 7
    return result
}

pn main() {
    print(concat_chain_tune22()); print(" ")
    print(concat_mixed_tune22()); print("\n")
}
