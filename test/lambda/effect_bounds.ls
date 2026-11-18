// S12.1.6v2: a procedure bound follows the value across a callback contract.
type Clock = pn () -> float^ @clock
type ClockAndPrint = pn () -> float @{clock, print}
pn tick() float @clock { clock() }
pn decide(f: Clock) float^ @clock { f()^ }
pn other() float @{clock, print} {
    print("")
    tick()
}
[tick is Clock, tick is ClockAndPrint, other is Clock]
function() {
    if (decide(tick)^ is float) 42 else 0
}
