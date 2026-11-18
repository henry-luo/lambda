// S12.1.4v7: a pure caller must provide a compatible callback.
pn sample() float @clock { clock() }
function choose(f: function = sample) any^ => f()^
fn pure_choice() any^ => choose()^
