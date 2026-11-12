// S7.7.2v2: a failed optimized literal keeps its declaration outcome and effects run once.
type Cell = {k: int}
pn missing(var calls) any {
    calls = calls + 1
    return parse("null", 'json')^
}
pn main() {
    var calls = 0
    let cell: Cell = {k: missing(calls)}
    print([cell is error, calls])
}
