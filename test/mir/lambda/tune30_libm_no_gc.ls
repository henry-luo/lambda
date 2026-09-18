// Tune30 T30-3: the libm leaves the sys-func table reaches by native pointer
// carry NO_GC / REENTRY_NO metadata, so a call to one is not an allocation
// point: the caller neither spills its roots before it nor reloads its
// container layouts after it. nbody paid 25 instructions per inner iteration
// for exactly this before the metadata landed.
pn tune30_norms(v: float[], n: int) float {
    var total: float = 0.0
    var i: int = 0
    while (i < n) {
        total = total + math.sqrt(v[i]) + math.sin(v[i])
        i = i + 1
    }
    return total
}

pn main() {
    var v: float[] = [1.0, 4.0, 9.0, 16.0]
    print(tune30_norms(v, 4))
    print("\n")
}
