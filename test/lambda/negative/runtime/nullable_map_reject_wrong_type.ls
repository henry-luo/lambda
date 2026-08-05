// A typed nullable lane accepts only its base type or null.
type Row = {value: int?}

fn dynamic(value) any { value }

pn main() {
    var row: Row = {value: 1}
    row.value = dynamic("wrong")
}
