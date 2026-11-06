// S9.1.4v2/D4.1.4: hidden alias dependencies can share a spelling and slot.
fn outer(T: type) {
    type Outer = T
    return (T: type) => {
        type Inner = T
        return (value: {left:Outer, right:Inner}) =>
            [type(value.left), type(value.right)]
    }
}
let make_inner = outer(float)
let reader = make_inner(int)
let other = outer(int)(float)
let churn = [for (i in 1 to 80) {text:string(i)}];
[
    reader({left:1, right:2.0}) == [float, int],
    other({left:1.0, right:2}) == [int, float],
    reader({left:"bad", right:1}) is error,
    len(churn) == 80
]
