// S4.1/D5.2/D5.3: a non-null typed read keeps its diagnostic on the OOB arm.
pn read_sum(xs: float[], ys: float[], index: int) float {
    let left: float = xs[index]
    let right: float = ys[index]
    return left + right
}

pn main() {
    print(read_sum([1.5, 2.5], [3.5, 4.5], 1))
}
