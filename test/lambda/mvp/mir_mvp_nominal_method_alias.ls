// S11.3.1v2: an alias retains the nominal descriptor and its complete methods.
fn make() {
    type Box {
        value: int,
        fn first() => value
        fn second() => value * 2
        fn third() => value * 3
    }
    type Alias = Box
    return <Alias value:7>
}
let instance = make();
[instance.first() == 7, instance.second() == 14, instance.third() == 21]
