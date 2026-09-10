fn count(values: int[]) int => len(values)
fn count_forward(values: int[]) int => count(values)

pn borrowed() {
    var values = [1, 2, 3]
    let observed = values
    let size = count_forward(observed)
    values[0] = 9
    return [size, values[0]]
}

pn snapshot() {
    var values = [1, 2, 3]
    let observed = values
    values[0] = 9
    return [observed[0], values[0]]
}

fn first(values: array) => values[0]

pn escaped_child() {
    var values = [[1]]
    let observed = values
    let child = first(observed)
    values[0][0] = 9
    return [child[0], values[0][0]]
}

pn main() {
    print([borrowed(), snapshot(), escaped_child()])
}
