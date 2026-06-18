function noop() {}

let a0 = { v: "o0" };
let a1 = { v: "o1" };
let a2 = { v: "o2" };
let a3 = { v: "o3" };
let a4 = { v: "o4" };
let a5 = { v: "o5" };
let a6 = { v: "o6" };
let a7 = { v: "o7" };
let a8 = { v: "o8" };
let a9 = { v: "o9" };
let a10 = { v: "o10" };
let a11 = { v: "o11" };
let a12 = { v: "o12" };
let a13 = { v: "o13" };
let a14 = { v: "o14" };
let a15 = { v: "o15" };
let a16 = { v: "o16" };
let a17 = { v: "o17" };
let a18 = { v: "o18" };
let a19 = { v: "o19" };

let outer = function() {
    return a0.v + "|" + a1.v + "|" + a2.v + "|" + a3.v + "|" +
        a4.v + "|" + a5.v + "|" + a6.v + "|" + a7.v + "|" +
        a8.v + "|" + a9.v + "|" + a10.v + "|" + a11.v + "|" +
        a12.v + "|" + a13.v + "|" + a14.v + "|" + a15.v + "|" +
        a16.v + "|" + a17.v + "|" + a18.v + "|" + a19.v;
};

{
    let a0 = { v: "i0" };
    let a1 = { v: "i1" };
    let a2 = { v: "i2" };
    let a3 = { v: "i3" };
    let a4 = { v: "i4" };
    let a5 = { v: "i5" };
    let a6 = { v: "i6" };
    let a7 = { v: "i7" };
    let a8 = { v: "i8" };
    let a9 = { v: "i9" };
    let a10 = { v: "i10" };
    let a11 = { v: "i11" };
    let a12 = { v: "i12" };
    let a13 = { v: "i13" };
    let a14 = { v: "i14" };
    let a15 = { v: "i15" };
    let a16 = { v: "i16" };
    let a17 = { v: "i17" };
    let a18 = { v: "i18" };
    let a19 = { v: "i19" };

    let inner = function() {
        return a19.v + "|" + a18.v + "|" + a17.v + "|" + a16.v + "|" +
            a15.v + "|" + a14.v + "|" + a13.v + "|" + a12.v + "|" +
            a11.v + "|" + a10.v + "|" + a9.v + "|" + a8.v + "|" +
            a7.v + "|" + a6.v + "|" + a5.v + "|" + a4.v + "|" +
            a3.v + "|" + a2.v + "|" + a1.v + "|" + a0.v;
    };
    inner();
}

noop();
console.log(outer());
