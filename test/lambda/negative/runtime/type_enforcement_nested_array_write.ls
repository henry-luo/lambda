fn dynamic(value) => value

type Variable = {value: int}

pn main() {
    var values: Variable[][] = dynamic([[{value: 1.0}]])
    values[0][0] = dynamic({strength: 1})
}
