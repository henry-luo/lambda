var item = document.getElementById("item");
console.log(getComputedStyle(item).width);
console.log(item.offsetWidth);
console.log(item.clientWidth);

var tile = document.getElementById("tile");
var dynamicStyle = document.createElement("style");
dynamicStyle.textContent = "#tile { width: 30px; }";
document.body.appendChild(dynamicStyle);
console.log(getComputedStyle(tile).width);
document.body.removeChild(dynamicStyle);
console.log(getComputedStyle(tile).width);

var cascadeTarget = document.getElementById("cascade-target");
var cascadeLast = document.getElementById("cascade-last");
var orderedStyle = document.createElement("style");
orderedStyle.textContent = "#cascade-target { width: 30px; }";
document.head.insertBefore(orderedStyle, cascadeLast);
console.log(getComputedStyle(cascadeTarget).width);
document.head.appendChild(orderedStyle);
console.log(getComputedStyle(cascadeTarget).width);
document.head.removeChild(orderedStyle);
console.log(getComputedStyle(cascadeTarget).width);

console.log(getComputedStyle(document.getElementById("abs-target")).width);
console.log(getComputedStyle(document.getElementById("invalid-line-height")).lineHeight);
console.log(getComputedStyle(document.getElementById("number-line-height-child")).lineHeight);
console.log(getComputedStyle(document.getElementById("percentage-line-height-child")).lineHeight);
var staticPositionParent = document.getElementById("static-position-parent");
var staticPositionAnchor = document.getElementById("static-position-anchor");
console.log(Math.round(staticPositionParent.getBoundingClientRect().width) + ":" +
    Math.round(staticPositionAnchor.getBoundingClientRect().x));

// CSSOM View includes descendant scrollable overflow in the root scroll area.
console.log(document.documentElement.scrollWidth + ":" + document.body.scrollWidth);
