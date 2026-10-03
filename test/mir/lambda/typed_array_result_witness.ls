// D2.8.2/D8.2.4v2: a success contract never erases a defect or a rebind.
fn probabilities() float[] => [0.25, 0.75]
pn failed_probabilities(count) float[] {
    var values: float[] = fill(count, 0.5)
    return values
}
pn read_probabilities(index: int) {
    var values = probabilities()
    return values[index]
}
pn changed_probabilities() {
    var values = probabilities()
    values = ["changed"]
    return values[0]
}
// S11.4.3: the untyped argument guard admits success once and forwards defects.
pn first_value(values) { return values[0] }
pn guarded_fill(count) { return first_value(fill(count, 0.5)) }
pn main() {
    print([read_probabilities(0), read_probabilities(1),
        read_probabilities(2) == null]) print("\n")
    var failed = failed_probabilities(-1)
    print([failed is error, failed[0] == null]) print("\n")
    print(changed_probabilities()) print("\n")
    print([guarded_fill(2), guarded_fill(-1) is error,
        guarded_fill(0) == null]) print("\n")
}
