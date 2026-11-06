// S9.1.4v2/D6.2.3: live parent homes and pure snapshots own scalar bytes.
fn scalar(seed) {
    var value: float = seed
    pn write() { value = 99.0 }
    let before = () any => value
    value = 2
    let first = before()
    let after = value
    return [first == 1.0, first is float, after == 2.0, after is float]
}
fn branch(flag) {
    let result = if (flag) {
        var value = 3
        pn write() { value = 9 }
        let saved = () => value
        value = 4
        saved()
    } else {
        var value = 7
        pn write() { value = 9 }
        let saved = () => value
        value = 8
        saved()
    }
    // a later collecting call must not reload homes from the exited branch.
    let later = sum([1, 2])
    return result + later
}
fn outcomes(input: any) {
    var value: float = input
    pn write() { value = 9.0 }
    let before = () any => value
    value = 2
    let original = before()
    return [original is error, value == 2.0, value is float]
}
fn nullable() {
    var small_value: int? = null
    var float_value: float? = null
    var flag: bool? = null
    pn write() { small_value = 9; float_value = 9.0; flag = true }
    let before = () => [small_value, float_value, flag]
    small_value = 2
    float_value = 2
    flag = false
    return [before() == [null, null, null], small_value == 2,
        float_value == 2.0, float_value is float, flag == false]
};
[scalar(1), branch(true) == 6, branch(false) == 10, outcomes("bad"), nullable()]
