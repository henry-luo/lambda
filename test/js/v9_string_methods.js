// v9 String methods: replaceAll, padStart, padEnd, at, search, toString

// replaceAll
console.log("replaceAll:", "hello world hello".replaceAll("hello", "hi"));

// padStart — pad to target length
console.log("padStart:", "5".padStart(3, "0"));
console.log("padStart sp:", "hi".padStart(5));
console.log("padStart long:", "hello".padStart(3, "0"));

// padEnd — pad to target length
console.log("padEnd:", "5".padEnd(3, "0"));
console.log("padEnd sp:", "hi".padEnd(5));

// at — positive and negative index
let s = "hello";
console.log("at 0:", s.at(0));
console.log("at -1:", s.at(-1));
console.log("at 4:", s.at(4));

// search — find position
console.log("search:", "hello world".search("world"));
console.log("search miss:", "hello world".search("xyz"));

// toString
console.log("toString:", "test".toString());
