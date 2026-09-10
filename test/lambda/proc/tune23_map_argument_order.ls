type Pair = {count: int, label: string}

fn dynamic(value) any => value

pn observe(value: string) string {
    print(value ++ "\n")
    return value
}

pn consume(pair: Pair, trailing: string) int {
    return pair.count
}

pn main() {
    print(consume({count: dynamic(7), label: observe("open label")}, observe("open trailing")) ++ "\n")
    print(consume({count: 8, label: observe("typed label")}, observe("typed trailing")) ++ "\n")
}
