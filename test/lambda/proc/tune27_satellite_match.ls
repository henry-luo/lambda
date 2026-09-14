// Tune27 T27-6: a `match` whose arms are type names and literals is admitted to
// the auto-tier satellite; the value, error and default arms must give the same
// results as T0 (D8.1.1v9).
pn tune27_step(x: int) int {
    if (x % 7 == 0) { return x + 3 }
    return x * 2
}

pn tune27_classify(v: any) int {
    let r = match v {
        case int: v + 1
        case string: len(v)
        case "": -2
        case null: -3
        default: -1
    }
    return r
}

pn tune27_satellite_match(n: int) int {
    var total: int = 0
    var i: int = 0
    while (i < n) {
        let raw = tune27_step(i)
        let picked = match raw {
            case int: raw
            default: 0
        }
        total = total + picked + tune27_classify(i) + tune27_classify("ab")
        i = i + 1
    }
    return total + tune27_classify(null) + tune27_classify(1.5)
}

pn main() {
    print(tune27_satellite_match(2000))
    print("\n")
}
