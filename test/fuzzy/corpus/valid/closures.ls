// Closures
fn make_adder(n: int) {
    fn add(x: int) => x + n;
    add
}

fn make_counter(start: int) {
    fn count(step: int) => start + step;
    count
}

fn make_multiplier(factor) {
    fn mult(x) => x * factor;
    mult
}

// Use closures
let add10 = make_adder(10);
let add20 = make_adder(20);
let counter = make_counter(100);
let double = make_multiplier(2);
let triple = make_multiplier(3);

add10(5) + add20(5) + counter(5) + double(5) + triple(5)
