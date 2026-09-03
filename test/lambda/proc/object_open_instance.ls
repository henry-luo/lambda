// S2.1.4 part 3 / OB16: a nominal instance is SEALED to its type but OPEN in
// its fields. Adding an undeclared field is an ordinary member addition and a
// shape transition, and every shape reached that way shares the ONE nominal
// record — so the value stays an instance of its type, its methods still
// resolve, and the declared prefix keeps its layout.

type P { x: int, fn dbl() => x * 2 }
type B { label: string, string* }

pn main() {
    var p = <P x: 5>
    p.z = 9

    // the declared prefix is untouched and the extra field reads back
    print(p.x); print(" "); print(p.z); print("\n")

    // still an instance of its type, and still nominal
    print(p is P); print(" "); print(p is object); print(" "); print(p is map); print("\n")

    // methods survive the transition — this is what LR03-8 was about
    print(p.dbl()); print("\n")

    // len counts the grown key set (S8.3.1v2)
    print(len(p)); print("\n")

    // printing round-trips, extra field included
    print(p); print("\n")

    // S5.4.2v3: equality is structural over the FULL key set, so a grown
    // instance does not equal an ungrown one of the same type
    var q = <P x: 5>
    print(p == q); print(" ")
    q.z = 9
    print(p == q); print("\n")

    // an element-shaped nominal value grows through its attribute face while
    // its content is left alone
    var b = <B label: "t", "c">
    b.extra = 1
    print(b.label); print(" "); print(b.extra); print(" "); print(b[0]); print(" ");
    print(b is B); print(" "); print(len(b)); print("\n")
}
