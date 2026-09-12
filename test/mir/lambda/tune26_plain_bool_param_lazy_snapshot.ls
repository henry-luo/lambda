// Tune26 T26-4: a mutated plain bool[] parameter snapshots lazily. A write-free
// activation must not detach, while a writing activation leaves its caller intact.

pn tune26_plain_bool_param_lazy_snapshot(values: bool[], write: bool) int {
    if (not write) {
        return if (values[0]) 1 else 0
    }
    values[0] = true
    values[1] = false
    return if (values[0] and not values[1]) 2 else 0
}

pn main() {
    var values: bool[] = [false, true]
    print(tune26_plain_bool_param_lazy_snapshot(values, false)); print(" ")
    print(tune26_plain_bool_param_lazy_snapshot(values, true)); print(" ")
    print(if (not values[0] and values[1]) 3 else 0); print("\n")
}
