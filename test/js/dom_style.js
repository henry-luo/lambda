// DOM Style Tests - inline style get/set, getComputedStyle

// --- Read inline style (length values) ---
var styled = document.getElementById("styled");
console.log(styled.style.padding); // 5px

// --- Set and read back inline style ---
var plain = document.getElementById("plain");
plain.style.color = "green";
console.log(plain.style.color); // green

plain.style.fontSize = "24px";
console.log(plain.style.fontSize); // 24px

plain.style.marginTop = "10px";
console.log(plain.style.marginTop); // 10px

// --- Set multiple styles ---
plain.style.width = "200px";
console.log(plain.style.width); // 200px

plain.style.display = "block";
console.log(plain.style.display); // block

// --- getComputedStyle ---
var cs = getComputedStyle(styled);
console.log(typeof cs); // object

// computed style should include stylesheet values
var csColor = cs.color;
console.log(csColor !== ""); // true

// computed style should include inline values
var csBg = cs.backgroundColor;
console.log(csBg !== ""); // true

// --- getComputedStyle on element with set styles ---
var cs2 = getComputedStyle(plain);
var plainColor = cs2.color;
console.log(plainColor !== ""); // true

// --- setAttribute class and className ---
var el = document.createElement("div");
el.setAttribute("class", "test-class");
console.log(el.getAttribute("class")); // test-class
console.log(el.className); // test-class

// --- setAttribute id ---
el.setAttribute("id", "dynamic-el");
console.log(el.id); // dynamic-el

"DOM style tests complete";
