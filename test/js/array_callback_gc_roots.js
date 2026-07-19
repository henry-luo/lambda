const source = Array.from(new Set(["alpha", "beta", "gamma"]));

const filtered = source.filter(value => {
    gc();
    return value.length > 0;
});
console.log(filtered.join(","));

const reduced = filtered.reduce((result, value) => {
    gc();
    return result + ":" + value;
});
console.log(reduced);

const chained = Array.from(new Set([
    String.fromCodePoint(0x1F600),
    String.fromCodePoint(0x1F642),
    String.fromCodePoint(0x1F680)
])).filter(value => {
    gc();
    const churn = {value: 1};
    return churn.value === 1 && value.length > 0;
});
console.log(chained.length, typeof chained[0], Array.from(chained[0]).length);
