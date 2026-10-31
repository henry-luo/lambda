// LR12-31 / S1.6: row replacement keeps leading-axis structure on every tier.
pn main() {
    var m = [[1, 2], [3, 4]]
    m[0] = [9, 9]
    print(m)
    print("\n")
    m[1] = [7, 8, 9]
    print(m)
    print("\n")
    var cube = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
    cube[0] = [[9, 8], [7, 6]]
    print(cube)
    print("\n")
}
