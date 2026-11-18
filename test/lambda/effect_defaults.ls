// S12.1.4v7: an omitted callback contributes the effects of its default.
pn sample() float @clock { clock() }
function choose(f: function = sample) any^ => f()^
fn pure_choice() any^ => choose(() => 7)^
fn checked_choice() any^ => call([choose][0], []) ^ { true }
let checked = checked_choice()^
function() {
    let accepted = choose()^;
    [accepted is float, pure_choice()^, checked]
}
