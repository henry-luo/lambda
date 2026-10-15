function canvas(width, height) {
    const result = document.createElement('canvas');
    result.width = width;
    result.height = height;
    return result;
}
function redRect(target, x, y, width, height) {
    const context = target.getContext('2d');
    context.fillStyle = 'red';
    context.fillRect(x, y, width, height);
}
const source = canvas(4, 2);
redRect(source, 0, 0, 4, 2);
const actual = canvas(8, 8);
const expected = canvas(8, 8);
const context = actual.getContext('2d');
context.drawImage(source, 2, 3);
redRect(expected, 2, 3, 4, 2);
console.log(actual.toDataURL() === expected.toDataURL());
context.clearRect(0, 0, 8, 8);
expected.getContext('2d').clearRect(0, 0, 8, 8);
context.drawImage(source, 0, 0, 8, 4);
redRect(expected, 0, 0, 8, 4);
console.log(actual.toDataURL() === expected.toDataURL());
context.clearRect(0, 0, 8, 8);
expected.getContext('2d').clearRect(0, 0, 8, 8);
context.drawImage(source, 1, 0, 2, 2, 3, 2, 2, 2);
redRect(expected, 3, 2, 2, 2);
console.log(actual.toDataURL() === expected.toDataURL());
context.clearRect(0, 0, 8, 8);
expected.getContext('2d').clearRect(0, 0, 8, 8);
context.save();
context.translate(2, 1);
context.beginPath();
context.rect(0, 0, 2, 2);
context.clip();
context.drawImage(source, 0, 0);
context.restore();
redRect(expected, 2, 1, 2, 2);
console.log(actual.toDataURL() === expected.toDataURL());
const self = canvas(4, 2);
const selfExpected = canvas(4, 2);
redRect(self, 0, 0, 2, 2);
self.getContext('2d').drawImage(self, 1, 0);
redRect(selfExpected, 0, 0, 3, 2);
console.log(self.toDataURL() === selfExpected.toDataURL());
let rejected = false;
try { context.drawImage(document.body, 0, 0); }
catch (error) { rejected = error instanceof TypeError; }
console.log(rejected);
const svgImage = document.createElementNS('http://www.w3.org/2000/svg', 'image');
svgImage.setAttribute('href', source.toDataURL());
context.clearRect(0, 0, 8, 8);
expected.getContext('2d').clearRect(0, 0, 8, 8);
context.drawImage(svgImage, 2, 3);
redRect(expected, 2, 3, 4, 2);
console.log(actual.toDataURL() === expected.toDataURL());
