var bareImage = Image();
var sizedImage = new Image(37, 41);

console.log(typeof Image);
console.log(bareImage.tagName);
console.log(bareImage.parentNode === null);
console.log(bareImage instanceof Image);
console.log(bareImage instanceof HTMLImageElement);
console.log(Image.prototype === HTMLImageElement.prototype);
console.log(bareImage.getAttribute("width") === null);
console.log(sizedImage.getAttribute("width"));
console.log(sizedImage.getAttribute("height"));
