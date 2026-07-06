fn make_adder(n) {
    fn add(x) => x + n
    add
}

let f = make_adder(10)
let g = make_adder(10)
let h = make_adder(20)

'same closure variable'
f == f

'same site, equal captures'
f == g

'same site, different captures'
f == h
f != h
