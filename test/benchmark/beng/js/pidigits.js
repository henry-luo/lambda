// BENG Benchmark: pi-digits (Node.js reference)
// Unbounded spigot algorithm (Gibbons 2004) using BigInt

const NUM_DIGITS = parseInt(process.argv[2] || "30");

function idiv(a, b) {
    // Floor division for BigInts
    const d = a / b;
    // JS BigInt division truncates toward zero; adjust for negative
    if ((a < 0n) !== (b < 0n) && d * b !== a) return d - 1n;
    return d;
}

let q = 1n, r = 0n, s = 0n, t = 1n, k = 0n;
let i = 0;
let digits = '';

while (i < NUM_DIGITS) {
    k = k + 1n;
    const k2 = k * 2n + 1n;

    // compose: multiply LFT by next term
    const new_q = q * k;
    const new_r = (2n * q + r) * k2;
    const new_s = s * k;
    const new_t = (2n * s + t) * k2;
    q = new_q;
    r = new_r;
    s = new_s;
    t = new_t;

    // can we extract a digit?
    if (q <= r) {
        const fd3 = idiv(3n * q + r, 3n * s + t);
        const fd4 = idiv(4n * q + r, 4n * s + t);
        if (fd3 === fd4) {
            digits += fd3.toString();
            i++;

            if (i % 10 === 0) {
                console.log(`${digits}\t:${i}`);
                digits = '';
            }

            // reduce: eliminate the extracted digit
            r = (r - fd3 * t) * 10n;
            q = q * 10n;
        }
    }
}

if (digits.length > 0) {
    const pad = ' '.repeat(10 - digits.length);
    console.log(`${digits}${pad}\t:${i}`);
}
