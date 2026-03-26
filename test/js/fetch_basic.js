// fetch() API tests - Phase 5
// Uses httpbin.org for testing HTTP operations

// Test 1: Basic GET request
fetch("https://httpbin.org/get").then(function(response) {
    console.log("status:" + response.status);
    console.log("ok:" + response.ok);
    return response.json();
}).then(function(data) {
    console.log("url:" + data.url);
});

// Test 2: POST request with body
fetch("https://httpbin.org/post", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: '{"key":"value"}'
}).then(function(response) {
    console.log("post_status:" + response.status);
    return response.json();
}).then(function(data) {
    console.log("post_data:" + data.data);
});

// Test 3: 404 response
fetch("https://httpbin.org/status/404").then(function(response) {
    console.log("404_status:" + response.status);
    console.log("404_ok:" + response.ok);
});

// Test 4: Response text()
fetch("https://httpbin.org/robots.txt").then(function(response) {
    return response.text();
}).then(function(text) {
    // robots.txt typically starts with "User-agent"
    console.log("has_text:" + (text.length > 0));
});

0;
