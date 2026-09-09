const queries = [];
for (let i = 0; i < 65; i++) {
    queries.push(matchMedia('(min-width: ' + i + 'px)'));
}

console.log(queries.length);
console.log(queries[64].media);
