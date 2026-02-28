// BENG Benchmark: fannkuch-redux (Node.js reference)

const N = parseInt(process.argv[2] || "7");

function fannkuch(n) {
    const perm = new Int32Array(n);
    const perm1 = new Int32Array(n);
    const count = new Int32Array(n);
    let maxFlips = 0, checksum = 0, permCount = 0;

    for (let i = 0; i < n; i++) perm1[i] = i;

    let r = n;
    while (true) {
        while (r !== 1) { count[r - 1] = r; r--; }

        for (let i = 0; i < n; i++) perm[i] = perm1[i];

        let flips = 0;
        let k = perm[0];
        while (k !== 0) {
            let lo = 0, hi = k;
            while (lo < hi) {
                const tmp = perm[lo]; perm[lo] = perm[hi]; perm[hi] = tmp;
                lo++; hi--;
            }
            flips++;
            k = perm[0];
        }

        if (flips > maxFlips) maxFlips = flips;
        checksum += (permCount % 2 === 0) ? flips : -flips;
        permCount++;

        // next permutation
        let found = false;
        r = 1;
        while (r < n) {
            const perm0 = perm1[0];
            for (let i = 0; i < r; i++) perm1[i] = perm1[i + 1];
            perm1[r] = perm0;
            count[r]--;
            if (count[r] > 0) { found = true; break; }
            r++;
        }
        if (!found) break;
    }

    console.log(checksum);
    console.log(`Pfannkuchen(${n}) = ${maxFlips}`);
}

fannkuch(N);
