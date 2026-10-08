// D7.4.5v2: generic map membership is independent of physical or virtual storage.
fn accept(value: map) => value.x
fn preserve(value: map) map => value
let dynamic = map(["x", 3, "label", "Lambda"])
let typed: map = dynamic;
[
    dynamic is map, dynamic is (map | null), dynamic is (map & any),
    [dynamic] is map[], accept(dynamic) == 3, typed.x == 3,
    preserve(dynamic) is map, preserve(dynamic).label == "Lambda",
    not (dynamic is element), not (dynamic is array), not (dynamic is object),
    {x: 3} is map, not (3 is map), not (null is map)
]
