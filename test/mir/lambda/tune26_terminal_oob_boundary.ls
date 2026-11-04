// S4.1/D6.2/D6.3: a non-null typed read keeps its diagnostic on the OOB arm.
pn read_sum(xs: float[], ys: float[], index: int) float^ {
    let _admitted_left_3: float = xs[index]
    let left: float = _admitted_left_3^
    let _admitted_right_4: float = ys[index]
    let right: float = _admitted_right_4^
    return left + right
}

pn main() {
    print(read_sum([1.5, 2.5], [3.5, 4.5], 1))
    print("\n")
    print([read_sum([1.5], [3.5], 1) is error,
        read_sum([1.5], [3.5], -1) is error,
        read_sum([1.5], [3.5, 4.5], 1) is error,
        read_sum([1.5, 2.5], [3.5], 1) is error])
    print("\n")
}
