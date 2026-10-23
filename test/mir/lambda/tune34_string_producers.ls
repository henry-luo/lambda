// D3.3.1/D8.3.3: concat width does not exhaust forwarding depth or erase misses.
pn assemble(a, b, c, d, e, f, g, h) {
    return "[" ++ a ++ ":" ++ b ++ ":" ++ c ++ ":" ++ d ++ ":" ++
        e ++ ":" ++ f ++ ":" ++ g ++ ":" ++ h ++ "]"
}
pn main() {
    let indirect = assemble
    print(assemble("a", "b", "c", "d", "e", "f", "g", "h")); print("\n")
    print(indirect("a", "b", "c", "d", "e", "f", "g", "h")); print("\n")
    print(type(indirect(error("join"), "b", "c", "d", "e", "f", "g", "h")) == error); print("\n")
    print(indirect(["x"], "b", "c", "d", "e", "f", "g", "h")); print("\n")
}
