// S12.1.6v3: an object method uses the caller's bound on every tier.
type EffectProbe {
    id: int = 0,
    pn read() float @clock { clock() }
    pn emit() float @print {
        print("unexpected-method-effect\n")
        1.0
    }
}

pn bounded() any^ @clock {
    var probe = <EffectProbe>
    let reading = probe.read()^;
    let refused = probe.emit() ^ { true };
    [reading is float, refused]
}

function() { bounded()^ }
