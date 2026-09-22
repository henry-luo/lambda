// Counted repetitions above RE2's limit must retain ECMAScript semantics via
// the backtracking engine; these are shapes loaded by live client bundles.
var payload = "";
for (var i = 0; i < 2048; i++) payload += "x";

console.log("bounded:" + /^[\s\S]{0,2048}$/.test(payload) + "," +
    /^[\s\S]{0,2048}$/.test(payload + "x"));

var token = "";
for (var j = 0; j < 16384; j++) token += "a";
console.log("token:" + /^[A-Za-z0-9_-]{80,16384}={0,2}$/.test(token) + "," +
    /^[A-Za-z0-9_-]{80,16384}={0,2}$/.test(token + "=" + "=" + "="));
