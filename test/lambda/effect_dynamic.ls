// S12.1.6v2: dynamic dispatch rejects a wider bound before executing it.
pn sample() float @clock { clock() }
pn emit_text() @print { print("unexpected") }
pn invoke(f: function) any^ @clock { f()^ }
function() {
    let accepted = invoke(sample)^
    let rejected = invoke(emit_text) ^ { true };
    [accepted is float, rejected]
}
