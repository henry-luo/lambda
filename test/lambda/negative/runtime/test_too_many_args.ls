// Test calling a function with too many arguments for closure (max 7)
fn make_adder(x) {
    fn inner(y) => x + y
    inner
}
let add5 = make_adder(5);
add5(1, 2, 3, 4, 5, 6, 7, 8, 9)  // 9 args exceeds closure max of 7
