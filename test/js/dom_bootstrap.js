// Test Bootstrap DOM enhancements
// Tests: Event/CustomEvent constructors, element.append(), getClientRects(),
//        Node.ELEMENT_NODE, expando properties, focus()/blur(), window.innerWidth

// === 1. Event constructor ===
var e = new Event('click');
console.log('Event type:', e.type);
console.log('Event bubbles:', e.bubbles);

var e2 = new Event('submit', { bubbles: true, cancelable: true });
console.log('Event2 bubbles:', e2.bubbles);
console.log('Event2 cancelable:', e2.cancelable);

// === 2. CustomEvent constructor ===
var ce = new CustomEvent('myevent', { detail: { foo: 42 }, bubbles: true });
console.log('CustomEvent type:', ce.type);
console.log('CustomEvent bubbles:', ce.bubbles);
console.log('CustomEvent detail.foo:', ce.detail.foo);

// === 3. Node constants ===
console.log('Node.ELEMENT_NODE:', Node.ELEMENT_NODE);
console.log('Node.TEXT_NODE:', Node.TEXT_NODE);
console.log('Node.DOCUMENT_NODE:', Node.DOCUMENT_NODE);
console.log('Node.DOCUMENT_FRAGMENT_NODE:', Node.DOCUMENT_FRAGMENT_NODE);

// === 4. window.innerWidth / innerHeight ===
console.log('innerWidth:', innerWidth);
console.log('innerHeight:', innerHeight);

// === 5. DOM methods (require document) ===
// These tests require a DOM document context, so we test feature detection
var div = document.createElement('div');
console.log('div created:', div.tagName);

// 5a. append()
var child1 = document.createElement('span');
var child2 = document.createElement('em');
div.append(child1, child2);
console.log('children after append:', div.childNodes.length);

// 5b. prepend()
var first = document.createElement('b');
div.prepend(first);
console.log('first child after prepend:', div.firstChild.tagName);

// 5c. getClientRects()
var rects = div.getClientRects();
console.log('getClientRects length:', rects.length);
console.log('getClientRects[0].width:', rects[0].width);

// 5d. focus/blur (should not throw)
div.focus();
div.blur();
console.log('focus/blur: ok');

// 5e. expando properties
div._bootstrapData = { tooltip: true, placement: 'top' };
console.log('expando get:', div._bootstrapData.tooltip);
console.log('expando placement:', div._bootstrapData.placement);

// store a number as expando
div._counter = 99;
console.log('expando counter:', div._counter);

console.log('ALL TESTS PASSED');
