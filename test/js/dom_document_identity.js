// The Document is one object, and it is a node (ESO101).
//
// It used to be two: a proxy carrying the document's own properties, and a
// "#document" node the tree walk reached. They answered different questions,
// and `documentElement.parentNode === document` was false because the module's
// parentNode ordinal read node->parent directly instead of asking the core --
// a second implementation of one operation, which is what ESO83 was about.

var el = document.getElementById("intro");

// identity: every route to the document arrives at the same object
console.log(el.ownerDocument === document);
console.log(document.documentElement.parentNode === document);
console.log(document.body.ownerDocument === document);

// it is a node
console.log(document.nodeType);
console.log(document.nodeName);
console.log(document.documentElement.parentNode.nodeType);

// and it still carries the Document's own properties
console.log(document.body.tagName);
console.log(document.documentElement.tagName);
console.log(document.title);

// the document has no parent
console.log(document.parentNode);
