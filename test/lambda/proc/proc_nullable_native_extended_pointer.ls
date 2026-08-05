// Typed pointer-backed optionals retain raw NULL lanes in shape fields and
// native Array words; each public read boxes only at the print boundary.
type Row = {amount: decimal?, at: datetime?, z: complex?}

fn dynamic(value) any { value }

pn main() {
    var source = {amount: 1.25m, at: t'2025-01-02', z: 2 + 3j}
    var target: Row = source
    target.amount = dynamic(null)
    target.at = dynamic(null)
    target.z = dynamic(null)

    var decimal_values = [1.25m, 2.5m]
    var datetime_values = [t'2025-01-02', t'2025-01-03']
    var complex_values = [2 + 3j, 4 + 5j]
    var decimals: decimal?[] = decimal_values
    var datetimes: datetime?[] = datetime_values
    var complexes: complex?[] = complex_values
    decimals[1] = dynamic(null)
    datetimes[1] = dynamic(null)
    complexes[1] = dynamic(null)

    print([source.amount, target.amount, source.at, target.at, source.z, target.z,
        decimals[0], decimals[1], datetimes[0], datetimes[1], complexes[0], complexes[1]])
}
