// DOM jQuery Support Tests
// Tests the Phase A-E features needed for jQuery 3.x support

// === Phase A: Quick Wins ===

// document.readyState should be "complete"
console.log(document.readyState);

// getComputedStyle should work
var app = document.getElementById("app");
var cs = getComputedStyle(app);
console.log(typeof cs);
console.log(cs.color !== "");

// === Phase B: DOM Event System ===

// addEventListener + dispatchEvent on element
var clicked = false;
app.addEventListener("click", function(e) {
    clicked = true;
    console.log(e.type);
    console.log(e.bubbles);
});
app.dispatchEvent({ type: "click", bubbles: true, cancelable: true });
console.log(clicked);

// addEventListener on document
var docEvent = false;
document.addEventListener("test", function(e) {
    docEvent = true;
    console.log(e.type);
});
document.dispatchEvent({ type: "test", bubbles: false });
console.log(docEvent);

// Event bubbling: child event bubbles to parent
var bubbled = false;
app.addEventListener("custom", function(e) {
    bubbled = true;
});
var target = document.getElementById("target");
target.dispatchEvent({ type: "custom", bubbles: true });
console.log(bubbled);

// removeEventListener
var removed = false;
var handler = function() { removed = true; };
app.addEventListener("rem", handler);
app.removeEventListener("rem", handler);
app.dispatchEvent({ type: "rem" });
console.log(removed);

// once option: listener fires once then removed
var onceCount = 0;
target.addEventListener("oncetest", function() {
    onceCount++;
}, { once: true });
target.dispatchEvent({ type: "oncetest" });
target.dispatchEvent({ type: "oncetest" });
console.log(onceCount);

// === Phase C: Timer Queue ===

// setTimeout should execute callback
setTimeout(function() {
    console.log("timer-ran");
}, 10);

// === Phase E: Layout Queries ===

// offsetWidth/offsetHeight exist and return numbers
console.log(typeof app.offsetWidth);
console.log(typeof app.offsetHeight);

// clientWidth/clientHeight exist
console.log(typeof app.clientWidth);
console.log(typeof app.clientHeight);

// offsetTop/offsetLeft exist
console.log(typeof app.offsetTop);
console.log(typeof app.offsetLeft);

// scrollTop/scrollLeft exist
console.log(typeof app.scrollTop);
console.log(typeof app.scrollLeft);

// getBoundingClientRect returns object with expected keys
var rect = app.getBoundingClientRect();
console.log(typeof rect);
console.log(typeof rect.top);
console.log(typeof rect.left);
console.log(typeof rect.width);
console.log(typeof rect.height);
