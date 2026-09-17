console.log(typeof document.domain);
console.log(document.domain === document.location.hostname);

let rejected = false;
try {
    document.domain = "example.com";
} catch (error) {
    rejected = error.name === "SecurityError";
}
console.log(rejected);
