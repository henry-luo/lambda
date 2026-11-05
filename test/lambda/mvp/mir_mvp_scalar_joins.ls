// D2.3.5/S7.9.3: an inferred scalar join retains a boxed producer's outcome
// until the enclosing callable boundary selects its successful result lane.
fn clip(el) => if (not (el is element)) false else string(name(el)) == "clipPath"
fn total(flag, x: float) => if (flag) 1.25 else sum([x, 2.0])
fn count(flag, x: int) => if (flag) 7 else sum([x, 2])
fn selected(flag, x: any) any => if (flag) false else x;
[
    clip(<clipPath>), not clip(<path>), not clip(4),
    total(true, 3.5) == 1.25, total(false, 3.5) == 5.5,
    count(true, 5) == 7, count(false, 5) == 7,
    selected(false, error("kept")).message == "kept",
    selected(true, error("unused")) == false
]
