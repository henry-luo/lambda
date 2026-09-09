const link = document.getElementsByTagName("link")[0];
const sheet = link.sheet;
console.log(typeof sheet);
console.log(sheet.cssRules.length > 0);
console.log(sheet.disabled);
console.log(link.disabled);
console.log(sheet.type);
