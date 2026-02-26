// Test string methods (v3: delegates to Lambda fn_* functions)

// trim
let s1 = "  hello  ";
console.log(s1.trim());

// toLowerCase / toUpperCase
let s2 = "Hello World";
console.log(s2.toLowerCase());
console.log(s2.toUpperCase());

// includes
console.log("hello world".includes("world"));
console.log("hello world".includes("xyz"));

// startsWith / endsWith
console.log("hello".startsWith("hel"));
console.log("hello".endsWith("llo"));
console.log("hello".startsWith("xyz"));

// indexOf / lastIndexOf
console.log("hello world hello".indexOf("hello"));
console.log("hello world hello".lastIndexOf("hello"));
console.log("hello".indexOf("xyz"));

// substring
console.log("hello world".substring(0, 5));
console.log("hello world".substring(6));

// replace
console.log("hello world".replace("world", "lambda"));

// charAt
console.log("hello".charAt(0));
console.log("hello".charAt(4));

// split
let parts = "a,b,c".split(",");
console.log(parts.length);

// concat
console.log("hello".concat(" ", "world"));

// length property
console.log("hello".length);
console.log("".length);

// trimStart / trimEnd
console.log("  hello  ".trimStart());
console.log("  hello  ".trimEnd());
