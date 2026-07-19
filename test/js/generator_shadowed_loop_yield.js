function* expand(ranges) {
    for (const [start, end] of ranges) {
        for (let ranges = start; ranges <= end; ranges++) {
            yield ranges;
        }
    }
}

console.log(Array.from(expand([[14, 17]])).join(','));

function* nested(value) {
    {
        let value = 3;
        yield value;
        value++;
        yield value;
    }
}

console.log(Array.from(nested(99)).join(','));
