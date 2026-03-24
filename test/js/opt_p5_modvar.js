// P5: Module variable arithmetic without boxing
// Module-level variables with known types (INT/FLOAT) can use inline
// arithmetic without box/unbox round-trips in compound assignments.

// Integer accumulator via +=
var total = 0;
for (var i = 1; i <= 10; i++) {
    total += i;
}
// total = 55 (triangular number)

// Integer accumulator via *=
var product = 1;
for (var j = 1; j <= 6; j++) {
    product *= j;
}
// product = 720 (6!)

// Accumulator with -=
var countdown = 100;
for (var k = 0; k < 10; k++) {
    countdown -= 7;
}
// countdown = 30

// Two module vars updated in a loop
var evens = 0;
var odds = 0;
for (var n = 1; n <= 20; n++) {
    if (n % 2 === 0) {
        evens += n;
    } else {
        odds += n;
    }
}
// evens = 2+4+6+8+10+12+14+16+18+20 = 110
// odds  = 1+3+5+7+9+11+13+15+17+19 = 100

// Running maximum with reassignment
var max_val = 0;
var vals = [3, 7, 2, 15, 6, 4, 11];
for (var m = 0; m < vals.length; m++) {
    if (vals[m] > max_val) {
        max_val = vals[m];
    }
}
// max_val = 15

{ total: total, product: product, countdown: countdown, evens: evens, odds: odds, max_val: max_val };
