type Row = {value: i64?}

fn dynamic(value) any { value }

pn main() {
    var source = {value: 7i64}
    var target: Row = source
    target.value = dynamic(null)
    target.value = 8i64
    print(string([source.value, target.value]) ++ "\n")
}
