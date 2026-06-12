// Minimal: just call withResolvers at module top level
const wr = Promise.withResolvers();
console.log("typeof promise:", typeof wr.promise);
console.log("typeof resolve:", typeof wr.resolve);
