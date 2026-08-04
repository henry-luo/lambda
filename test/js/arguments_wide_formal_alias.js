// Mapped sloppy arguments must cover every formal, not just a compiler table.
function writeback(a00, a01, a02, a03, a04, a05, a06, a07, a08,
                   a09, a10, a11, a12, a13, a14, a15, a16) {
    a16 = 99;
    return arguments.length + ":" + arguments[16];
}

function readback(a00, a01, a02, a03, a04, a05, a06, a07, a08,
                  a09, a10, a11, a12, a13, a14, a15, a16) {
    arguments[16] = 77;
    return a16;
}

console.log("writeback:" + writeback(0, 1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16));
console.log("readback:" + readback(0, 1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16));

function shadowed(a) {
    {
        let a = 2;
        a = 3;
    }
    return arguments[0] + ":" + a;
}

console.log("shadowed:" + shadowed(1));
