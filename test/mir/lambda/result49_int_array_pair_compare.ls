pn main() {
    let left: int[] = [1, 2, 3]
    let right: int[] = [1, 2, 4]
    var li: int = 0
    var ri: int = 0
    print(left[li] == right[ri]); print(" ")
    li = 2
    ri = 2
    print(left[li] != right[ri]); print(" ")
    li = -1
    ri = 3
    print(left[li] == right[ri]); print(" ")
    ri = 0
    print(left[li] == right[ri]); print("\n")

    let wide: int = 9007199254740991 + 1
    let poison: int = wide - wide
    let highs: int[] = [wide, wide]
    let poisons: int[] = [poison, poison]
    li = 0
    ri = 1
    print(highs[li] == highs[ri]); print(" ")
    print(poisons[li] == poisons[ri]); print("\n")
}
