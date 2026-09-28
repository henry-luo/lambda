// fetch() API tests - Phase 5
// Tests that work without external network dependencies

// Test 1: fetch with invalid URL should reject, and returns a promise
// A malformed authority fails deterministically without waiting for a socket timeout.
var p = fetch("http://[");
p.then(function(response) {
    console.log("should_not_reach");
}).catch(function(err) {
    console.log("error_caught:true");
    console.log("promise_catch:true");
});

0;
