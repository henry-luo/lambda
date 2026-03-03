// Larceny Benchmark: quicksort (Node.js)
// Quicksort with Lomuto partition
'use strict';

function lcgNext(seed) {
    return (seed * 1664525 + 1013904223) % 1000000;
}

function partition(arr, lo, hi) {
    const pivot = arr[hi];
    let i = lo;
    for (let j = lo; j < hi; j++) {
        if (arr[j] <= pivot) {
            const tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
            i++;
        }
    }
    const tmp = arr[i];
    arr[i] = arr[hi];
    arr[hi] = tmp;
    return i;
}

function quicksort(arr, lo, hi) {
    if (lo >= hi) return;
    const p = partition(arr, lo, hi);
    quicksort(arr, lo, p - 1);
    quicksort(arr, p + 1, hi);
}

function isSorted(arr, n) {
    for (let i = 1; i < n; i++) {
        if (arr[i] < arr[i - 1]) return false;
    }
    return true;
}

function main() {
    const size = 5000;
    const arr = new Int32Array(size);

    let seed = 42;
    for (let i = 0; i < size; i++) {
        seed = lcgNext(seed);
        arr[i] = seed;
    }

    quicksort(arr, 0, size - 1);

    if (isSorted(arr, size)) {
        process.stdout.write("quicksort: PASS\n");
    } else {
        process.stdout.write("quicksort: FAIL\n");
    }
}

main();
