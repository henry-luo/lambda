var child = document.getElementById('child');
var input = document.getElementById('input');
var circle = document.getElementById('circle');
var get = Object.getOwnPropertyDescriptor(Element.prototype, 'getAttribute');
var tag = Object.getOwnPropertyDescriptor(Element.prototype, 'tagName');
var parent = Object.getOwnPropertyDescriptor(Node.prototype, 'parentElement');

console.log(typeof get.value, get.value.length, get.writable, get.enumerable, get.configurable);
console.log(typeof tag.get, tag.set, tag.enumerable, tag.configurable);
console.log(get.value === child.getAttribute);
console.log(get.value.call(child, 'data-value'));
console.log(get.value.call(input, 'data-value'));
console.log(get.value.call(circle, 'data-value'));
console.log(tag.get.call(child));
console.log(parent.get.call(child).id);
console.log(parent.get.call(document));
console.log(Node.prototype.contains.call(document, child));

for (var receiver of [{}, Object.create(Element.prototype)]) {
    try { get.value.call(receiver, 'id'); } catch (error) { console.log(error.name); }
    try { tag.get.call(receiver); } catch (error) { console.log(error.name); }
}

// A prototype interceptor must affect every native subtype's instance reads.
Object.defineProperty(Element.prototype, 'getAttribute', {
    value: function(name) { return 'wrapped:' + get.value.call(this, name); },
    writable: true, configurable: true
});
console.log(child.getAttribute('data-value'));
console.log(input.getAttribute('data-value'));
console.log(circle.getAttribute('data-value'));
Object.defineProperty(Element.prototype, 'getAttribute', get);
console.log(child.getAttribute('data-value'));

var text = document.createTextNode('text');
Node.prototype.appendChild.call(child, text);
console.log(parent.get.call(text) === child);
console.log(Node.prototype.cloneNode.call(text, false).data);

var fragment = document.createDocumentFragment();
Node.prototype.appendChild.call(fragment, document.createElement('b'));
console.log(Object.getOwnPropertyDescriptor(DocumentFragment.prototype, 'querySelector').value.call(fragment, 'b').tagName);
console.log(fragment.childElementCount, fragment.children.length);
console.log(typeof fragment.getElementsByTagName, 'getElementsByTagName' in fragment);
try { Element.prototype.querySelector.call(null, 'b'); } catch (error) { console.log(error.name); }
var create = Object.getOwnPropertyDescriptor(Document.prototype, 'createElement');
console.log(create.value.length, create.value.call(document, 'i').tagName);
var foreign = document.implementation.createHTMLDocument('foreign');
console.log(create.value.call(foreign, 'b').tagName);
try { create.value.call(child, 'b'); } catch (error) { console.log(error.name); }

// Writable prototype methods create ordinary own properties on native receivers.
var setAttribute = child.setAttribute;
child.setAttribute = function(name, value) {
    return setAttribute.call(this, name, 'own:' + value);
};
child.setAttribute('data-value', 'replacement');
var ownSet = Object.getOwnPropertyDescriptor(child, 'setAttribute');
console.log(child.getAttribute('data-value'), ownSet.writable, ownSet.enumerable, ownSet.configurable);
console.log(delete child.setAttribute, child.setAttribute === setAttribute);
console.log(Reflect.set(Element.prototype, 'setAttribute', setAttribute, child));
console.log(Object.hasOwn(child, 'setAttribute'), child.setAttribute === setAttribute);
Object.defineProperty(child, 'setAttribute', {writable: false});
console.log(Reflect.set(Element.prototype, 'setAttribute', function() {}, child));
try { (function() { 'use strict'; child.setAttribute = function() {}; })(); }
catch (error) { console.log(error.name); }
delete child.setAttribute;
var receiverSeen;
Object.defineProperty(Element.prototype, 'intercepted', {
    set: function(value) { receiverSeen = this; this.expando = value; }, configurable: true
});
console.log(Reflect.set(Element.prototype, 'intercepted', 17, child), receiverSeen === child, child.expando);
delete Element.prototype.intercepted;
Object.defineProperty(Element.prototype, 'locked', {value: 19, writable: false, configurable: true});
console.log(Reflect.set(child, 'locked', 21), child.locked, Object.hasOwn(child, 'locked'));
delete Element.prototype.locked;
var collection = child.childNodes;
console.log(Reflect.set({}, 'marker', 23, collection), collection.marker, Object.hasOwn(collection, 'marker'));
console.log(Reflect.set({}, 'marker', 23, 5));
