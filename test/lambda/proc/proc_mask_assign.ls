// Masked assignment — arr[mask] = scalar | values (Typed Array 4, Scope 3.3).
// The write-counterpart to arr[mask] selection.  Writes in place where the bool
// mask is true; with an array RHS the values are consumed in order.  Procedural
// only (index assignment is a pn statement), so the functional core is unaffected.

pn test_scalar_mask() {
    var arr = [10, 20, 30, 40, 50]
    arr[arr gt 25] = 0                // -> [10, 20, 0, 0, 0]
    print(arr[0]); print(" "); print(arr[1]); print(" "); print(arr[2]); print(" ")
    print(arr[3]); print(" "); print(arr[4]); print("\n")
}

pn test_float_image_idiom() {
    var img = [1.0, 2.0, 3.0, 4.0, 5.0]
    img[img lt 3.0] = 0.0             // the skimage idiom: zero dark pixels -> [0, 0, 3, 4, 5]
    print(img[0]); print(" "); print(img[2]); print(" "); print(img[4]); print("\n")
}

pn test_block_values() {
    var b = [10, 20, 30, 40, 50]
    b[b gt 25] = [100, 200, 300]      // selected slots take 100,200,300 in order
    print(b[2]); print(" "); print(b[3]); print(" "); print(b[4]); print("\n")
}

pn test_2d_mask() {
    var m = [[10, 200, 30], [240, 50, 250]]
    m[m gt 100] = 0                   // threshold-zero bright pixels across the whole image
    print(m[0][0]); print(" "); print(m[0][1]); print(" "); print(m[0][2]); print("\n")  // 10 0 30
    print(m[1][0]); print(" "); print(m[1][1]); print(" "); print(m[1][2]); print("\n")  // 0 50 0
}

pn main() {
    test_scalar_mask()
    test_float_image_idiom()
    test_block_values()
    test_2d_mask()
}
