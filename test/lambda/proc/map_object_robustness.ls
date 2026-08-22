// Procedural half of test/lambda/map_object_robustness.ls.
// A `pn` method may only be called from procedural context (S12.1) and a
// mutating method needs a `var` receiver (S9), so the object-mutation
// sections live here. Every purely functional case stays in the functional
// test and reports its value directly.

type Counter { val: int = 0, pn add(n: int) { val = val + n } }
type Accum { total: int = 0, pn add(n: int) { total = total + n } }
type Wallet {
    balance: int = 0,
    pn deposit(n: int) {
        balance = balance + n
    }
    pn withdraw(n: int) {
        balance = balance - n
    }
}
type Toggle {
    on: bool = false,
    pn flip() {
        on = not on
    }
}

pn main() {
    // 8b: pn method mutating int field
    var cnt = <Counter val: 10>
    print('=8b='); print("\n")
    cnt.add(5)
    print(cnt.val); print("\n")

    // 8c: multiple pn calls in sequence
    var ac = <Accum total: 0>
    print('=8c='); print("\n")
    ac.add(10)
    ac.add(20)
    ac.add(30)
    print(ac.total); print("\n")

    // ============================================================
    // Section 10: Object mutation (pn) and read-back
    // ============================================================

    // 10a: int mutation and subsequent read
    var wallet = <Wallet balance: 100>
    print('=10a='); print("\n")
    wallet.deposit(50)
    wallet.withdraw(30)
    print(wallet.balance); print("\n")

    // 10b: bool toggle mutation
    var t = <Toggle on: false>
    print('=10b='); print("\n")
    print(t.on); print("\n")
    t.flip()
    print(t.on); print("\n")
    t.flip()
    print(t.on); print("\n")

}
