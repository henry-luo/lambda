// S12.1.4v7: a closure's colour follows its captured callback.
pn tick() float @clock { clock() }
function close(f: function) => () any^ => f()^
let pure = close(() => 7);
(pure is fn);
pure()^
function() {
    let closure = close(tick);
    [closure is pn, closure()^ is float]
}
