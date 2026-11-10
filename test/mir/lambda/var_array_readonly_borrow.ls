// D6.3.2/S9.1.3: a read-only var borrow needs no detach or collecting helper.
pn size(var values: float[]) int { return len(values) }
pn forward(var values: float[]) int { return size(values) }

pn main() {
    var values: float[] = [1.0, 2.0]
    let before = values
    print([forward(values), values, before])
}
