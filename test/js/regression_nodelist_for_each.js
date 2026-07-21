console.log(typeof NodeList.prototype.forEach);

var count = 0;
NodeList.prototype.forEach.call(document.querySelectorAll(".item"), function() {
    count++;
});
console.log(count);
