// Larceny Benchmark: triangl (Node.js)
// Triangle solitaire board puzzle — count solutions via backtracking
'use strict';

function main() {
    const __t0 = process.hrtime.bigint();
    const mfrom = [0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14];
    const mover = [1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13];
    const mto   = [3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12];
    const numMoves = 36;

    const board = new Uint8Array(15).fill(1);
    board[0] = 0;

    let solutions = 0;
    let pegs = 14;
    const stack = new Int32Array(14);
    let depth = 0;

    while (depth >= 0) {
        if (pegs === 1) {
            solutions++;
            depth--;
            if (depth < 0) break;
            const lastM = stack[depth];
            board[mfrom[lastM]] = 1;
            board[mover[lastM]] = 1;
            board[mto[lastM]] = 0;
            pegs++;
            stack[depth] = lastM + 1;
        }

        let found = false;
        let mi = stack[depth];
        while (mi < numMoves) {
            if (board[mfrom[mi]] && board[mover[mi]] && !board[mto[mi]]) {
                board[mfrom[mi]] = 0;
                board[mover[mi]] = 0;
                board[mto[mi]] = 1;
                pegs--;
                stack[depth] = mi;
                depth++;
                if (depth < 14) stack[depth] = 0;
                found = true;
                break;
            }
            mi++;
        }

        if (!found) {
            depth--;
            if (depth >= 0) {
                const lastM = stack[depth];
                board[mfrom[lastM]] = 1;
                board[mover[lastM]] = 1;
                board[mto[lastM]] = 0;
                pegs++;
                stack[depth] = lastM + 1;
            }
        }
    }
    const __t1 = process.hrtime.bigint();

    process.stdout.write("triangl: solutions=" + solutions + "\n");
    if (solutions === 29760) {
        process.stdout.write("triangl: PASS\n");
    } else {
        process.stdout.write("triangl: DONE\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();
