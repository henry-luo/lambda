// Sized scalar arrays must widen by value when assigned to wider typed arrays.

pn main() {
    let xs = [1i8, -2i8]
    var ints: int[] = xs
    var anys: any[] = ints
    print((ints[0]) ++ "," ++ (ints[1]) ++ "\n")
    print((anys[0]) ++ "," ++ (anys[1]) ++ "\n")
}
