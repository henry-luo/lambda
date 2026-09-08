var observer = new ResizeObserver(function() {});
observer.observe(document.body);
document.body.appendChild(document.createElement("div"));
console.log("layout-pending");
