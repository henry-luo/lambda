// S7.7.2v2/D2.5.3: member reads retain a failed receiver's actual outcome.
type Box = {n: int, message: int, code: int}
fn opaque(value) => value
pn member_loop() {
    let initial: Box = opaque({n: 7, message: 8, code: 9})
    var box: Box = initial^
    var stage = 0
    var observed = []
    while (stage < 2) {
        let value = box.n
        let message = box.message
        observed = observed ++ [[value is error, message is string]]
        box = opaque("wrong")
        stage = stage + 1
    }
    observed
}
pn main() {
    let box: Box = opaque("wrong")
    let value = box.n
    let admitted: int^ = box.n
    let indexed = box["n"]
    let message = box.message
    let code = box.code;
    [value is error, admitted is error, indexed is error,
     message is string, code is int, (box.n ^ { 99 }), member_loop()]
}
