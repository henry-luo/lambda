// A statement handler that stores the handled error (`e = ^`) catches it in a
// `pn ... T^` too. The JIT treated the assignment's value -- that same error --
// as a failed statement of a can-raise procedure and returned it, so the
// handler caught nothing; T0 and `pn ... T` were right. A fresh error made in
// the handler body still propagates (S7.6.1, S7.6.7v4).
pn boom() int^ { raise error("boom") }
pn boom2() int^ { raise error("boom2") }

pn stored(url) map^ {
    var failure = null
    io.fetch(url) ^ { failure = ^ }
    {caught: failure is error, code: failure.code}
}
pn both_arms() map^ {
    var failure = null
    var value = null
    boom() ^ { failure = ^ } ~ { value = ~ }
    {caught: failure is error, value: value}
}
pn block_body() map^ {
    var notes = []
    var failure = null
    boom() ^ { notes = [*notes, "seen"]; failure = ^ }
    {caught: failure is error, notes: notes}
}
pn fresh() int^ {
    var failure = null
    boom() ^ { failure = boom2()^ }
    0
}
pn main() {
    let a: map | error = stored("test/no/such/file.css")
    let b: map | error = both_arms()
    let c: map | error = block_body()
    let d: int | error = fresh();
    [a, b, c, if (d is error) d.message else d]
}
