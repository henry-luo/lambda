console.log(document.scripts.length);
const last = document.scripts[document.scripts.length - 1];
console.log(last.tagName);
console.log(last.src.includes('dom_document_scripts.js'));
