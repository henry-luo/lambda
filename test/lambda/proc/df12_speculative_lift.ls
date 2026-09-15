// DF12: bare arithmetic may choose an int raw shape, but it is never a source
// contract. Exact int calls use the raw entry; floats and dynamic float Items
// must take the source-equivalent boxed slow body.
fn twice(value) => value + value

let dynamic_int: any = 9
let dynamic_float: any = 2.5

pn main() {
    print([twice(7), twice(2.5), twice(dynamic_int), twice(dynamic_float)])
}
