type Row = {value: int?, ready: bool?, ratio: float?}

fn dynamic(value) any { value }

pn main() {
    var source = {value: 7, ready: true, ratio: 1.5}
    var target: Row = source
    target.value = dynamic(null)
    target.ready = dynamic(null)
    target.ratio = dynamic(null)
    var initially_null: Row = {value: null, ready: null, ratio: null}
    initially_null.value = 8
    initially_null.ready = true
    initially_null.ratio = 2.5
    print(string([source.value, target.value, target.ready,
        target.ratio, initially_null.value, initially_null.ready,
        initially_null.ratio]) ++ "\n")
}
