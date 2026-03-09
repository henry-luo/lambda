// BENG Benchmark: mandelbrot (Node.js reference)
// XOR checksum approach (matches Lambda adaptation)

const N = parseInt(process.argv[2] || "500");

const __t0 = process.hrtime.bigint();
let checksum = 0;
for (let y = 0; y < N; y++) {
    let bits = 0, bitNum = 0;
    for (let x = 0; x < N; x++) {
        let cr = 2.0 * x / N - 1.5;
        let ci = 2.0 * y / N - 1.0;
        let zr = 0.0, zi = 0.0;
        let inside = 1;
        for (let i = 0; i < 50; i++) {
            const tr = zr * zr - zi * zi + cr;
            const ti = 2.0 * zr * zi + ci;
            zr = tr; zi = ti;
            if (zr * zr + zi * zi > 4.0) { inside = 0; break; }
        }
        bits = (bits << 1) | inside;
        bitNum++;
        if (bitNum === 8) {
            checksum ^= bits;
            bits = 0; bitNum = 0;
        }
    }
    if (bitNum > 0) {
        bits <<= (8 - bitNum);
        checksum ^= bits;
    }
}
const __t1 = process.hrtime.bigint();
console.log(checksum);
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
