// The static rectangle belongs to the anonymous cell's finalized text-align line.
var label = document.getElementById("static-label");
console.log(Math.round(label.getBoundingClientRect().x));
