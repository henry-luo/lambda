// A COW map clone must retain i8?'s widened lane through null and value writes.
type Row = {value: i8?}

fn dynamic(value) any { value }

pn main() {
    var source = {value: 7i8}
    var target: Row = source
    target.value = dynamic(null)
    target.value = -8i8
    print(string([source.value, target.value]) ++ "\n")
}
