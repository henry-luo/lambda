// S11.4.8v2/D4.1.4: method entry selects its declaring annotation environment.
fn make(T: type, extra: int) {
    type Box {
        value: int,
        fn selected(input: T = 1) T => input
        fn again(n: int) any => if (n == 0) extra else ~.again(n - 1)
        fn sibling() any => ~.again(2)
    }
    type Alias = Box
    fn construct() => <Alias value:5>
    return construct
}
let floating = make(float, 10)()
let integral = make(int, 20)()
let saved = floating.selected
let churn = [for (i in 1 to 100) {text:string(i)}];
[
    type(floating.selected()) == float,
    type(integral.selected()) == int,
    type(saved(2)) == float,
    floating.selected("bad") is error,
    floating.again(3) == 10,
    integral.sibling() == 20,
    len(churn) == 100
]
