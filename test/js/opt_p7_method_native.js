// P7: Native method dispatch for typed class instances
// When a var is a known class instance (P4 class_entry) and the called method
// has a native version (all INT/FLOAT params), the call bypasses generic
// boxing + runtime dispatch and uses a direct native call.

class Calculator {
    constructor(base) {
        this.base = base;  // P3: slot 0 — ensures ctor_prop_count > 0
    }
    // Pure computation methods - no this.prop access, all INT params.
    // These qualify for native version (P1/P4 type inference).
    add(a, b) {
        return a + b;
    }
    mul(a, b) {
        return a * b;
    }
    dot3(a, b, c) {
        return a * a + b * b + c * c;
    }
    fma(a, b, c) {
        return a * b + c;
    }
}

// var c has class_entry set (P4) because Calculator has ctor_prop_count > 0
var c = new Calculator(0);

// P7 fires: typed instance.method(int_args) with native version
var a1 = c.add(10, 20);       // 30
var m1 = c.mul(6, 7);         // 42
var d1 = c.dot3(1, 2, 2);     // 1 + 4 + 4 = 9
var f1 = c.fma(3, 4, 5);      // 12 + 5 = 17

// P7 in a loop - direct native dispatch per iteration
var sum = 0;
for (var i = 1; i <= 10; i = i + 1) {
    sum = sum + c.add(i, i * 2);  // i + 2i = 3i; sum = 3*(1+2+...+10) = 165
}

// Multiple instances - each triggers P7 independently
var c2 = new Calculator(100);
var a2 = c2.add(3, 7);        // 10
var m2 = c2.mul(4, 5);        // 20
var d2 = c2.dot3(2, 3, 6);   // 4 + 9 + 36 = 49

{ a1: a1, m1: m1, d1: d1, f1: f1, sum: sum, a2: a2, m2: m2, d2: d2 };
