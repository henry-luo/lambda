// R7RS Benchmark: fft (Node.js)
// Fast Fourier Transform from "Numerical Recipes in C"
// 1 iteration on 4096-element vector, expected result: 0.0
'use strict';

const PI2 = 6.28318530717959;

function four1(data, n) {
    // Bit-reversal section
    let i = 0;
    let j = 0;
    while (i < n) {
        if (i < j) {
            let temp = data[i];
            data[i] = data[j];
            data[j] = temp;
            temp = data[i + 1];
            data[i + 1] = data[j + 1];
            data[j + 1] = temp;
        }
        let m = n >> 1;
        while (m >= 2 && j >= m) {
            j = j - m;
            m = m >> 1;
        }
        j = j + m;
        i = i + 2;
    }

    // Danielson-Lanczos section
    let mmax = 2;
    while (mmax < n) {
        const theta = PI2 / mmax;
        const sinHalf = Math.sin(0.5 * theta);
        const wpr = -2.0 * sinHalf * sinHalf;
        const wpi = Math.sin(theta);

        let wr = 1.0;
        let wi = 0.0;
        let m2 = 0;
        while (m2 < mmax) {
            let ii = m2;
            while (ii < n) {
                const jj = ii + mmax;
                const tempr = wr * data[jj] - wi * data[jj + 1];
                const tempi = wr * data[jj + 1] + wi * data[jj];
                data[jj] = data[ii] - tempr;
                data[jj + 1] = data[ii + 1] - tempi;
                data[ii] = data[ii] + tempr;
                data[ii + 1] = data[ii + 1] + tempi;
                ii = ii + mmax + mmax;
            }
            const newWr = wr * wpr - wi * wpi + wr;
            wi = wi * wpr + wr * wpi + wi;
            wr = newWr;
            m2 = m2 + 2;
        }
        mmax = mmax * 2;
    }
}

function main() {
    const __t0 = process.hrtime.bigint();
    const data = new Float64Array(4096);
    four1(data, 4096);
    const result = data[0];
    const __t1 = process.hrtime.bigint();

    if (result === 0.0) {
        process.stdout.write("fft: PASS\n");
    } else {
        process.stdout.write("fft: FAIL result=" + result + "\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();
