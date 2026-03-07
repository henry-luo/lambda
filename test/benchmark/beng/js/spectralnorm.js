// BENG Benchmark: spectral-norm (Node.js reference)
// Eigenvalue approximation using power method

const N = parseInt(process.argv[2] || "100");

function a(i, j) {
    return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1);
}

function multiplyAv(n, v, av) {
    for (let i = 0; i < n; i++) {
        av[i] = 0;
        for (let j = 0; j < n; j++) {
            av[i] += a(i, j) * v[j];
        }
    }
}

function multiplyAtv(n, v, atv) {
    for (let i = 0; i < n; i++) {
        atv[i] = 0;
        for (let j = 0; j < n; j++) {
            atv[i] += a(j, i) * v[j];
        }
    }
}

function multiplyAtAv(n, v, atav) {
    const u = new Float64Array(n);
    multiplyAv(n, v, u);
    multiplyAtv(n, u, atav);
}

const __t0 = process.hrtime.bigint();
const u = new Float64Array(N).fill(1);
const v = new Float64Array(N);

for (let i = 0; i < 10; i++) {
    multiplyAtAv(N, u, v);
    multiplyAtAv(N, v, u);
}

let vbv = 0, vv = 0;
for (let i = 0; i < N; i++) {
    vbv += u[i] * v[i];
    vv += v[i] * v[i];
}

const __t1 = process.hrtime.bigint();
console.log(Math.sqrt(vbv / vv).toFixed(9));
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
