var owner = document.getElementById('owner');
var attribute = owner.getAttributeNode('data-value');
console.log(attribute instanceof Attr, attribute instanceof Node, attribute.nodeType, attribute.nodeName);
console.log(attribute.name, attribute.localName, attribute.namespaceURI, attribute.prefix, attribute.specified);
console.log(attribute.ownerElement === owner, attribute.ownerDocument === document, attribute.parentNode, attribute.isConnected);
console.log(owner.attributes.getNamedItem('data-value') === attribute, owner.attributes['data-value'] === attribute);
console.log(owner.attributes.item(1) === attribute, attribute.childNodes.length, attribute.firstChild, attribute.getRootNode() === attribute);
console.log(owner.attributes[1] === attribute, Array.from(owner.attributes)[1] === attribute);
var empty = owner.getAttributeNode('data-empty');
console.log(empty instanceof Attr, empty.value === '', owner.attributes[2] === empty);
attribute.value = 'changed';
console.log(owner.getAttribute('data-value'), attribute.nodeValue, attribute.textContent);
owner.setAttribute('data-value', 'ordinary');
console.log(attribute.value, owner.getAttributeNode('DATA-VALUE') === attribute);
attribute.nodeValue = null;
console.log(attribute.value === '', owner.getAttribute('data-value') === '');
attribute.textContent = 'text';
console.log(attribute.value, owner.getAttribute('data-value'));
var valueDescriptor = Object.getOwnPropertyDescriptor(Attr.prototype, 'value');
console.log(typeof valueDescriptor.get, typeof valueDescriptor.set, valueDescriptor.enumerable, valueDescriptor.configurable);
var clone = attribute.cloneNode();
console.log(clone instanceof Attr, clone !== attribute, clone.isEqualNode(attribute), clone.ownerElement, clone.value);
var replacement = document.createAttribute('DATA-VALUE');
replacement.value = 'replacement';
console.log(replacement.name, owner.setAttributeNode(replacement) === attribute);
console.log(attribute.ownerElement, attribute.value, replacement.ownerElement === owner, owner.attributes.getNamedItem('data-value') === replacement);
console.log(owner.setAttributeNodeNS(replacement) === replacement);
var other = document.createElement('p');
try { other.setAttributeNode(replacement); } catch (error) { console.log(error.name); }
console.log(owner.removeAttributeNode(replacement) === replacement, replacement.ownerElement, replacement.value);
try { owner.removeAttributeNode(replacement); } catch (error) { console.log(error.name); }
replacement.value = 'detached';
console.log(owner.getAttribute('data-value'), replacement.value);
console.log(owner.attributes.setNamedItem(replacement), owner.attributes.getNamedItem('DATA-VALUE') === replacement);
console.log(owner.attributes.removeNamedItem('data-value') === replacement, replacement.ownerElement);
try { owner.attributes.removeNamedItem('data-value'); } catch (error) { console.log(error.name); }
owner.setAttribute('data-value', 'new');
console.log(owner.getAttributeNode('data-value') !== replacement, replacement.value);

var svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
var namespaced = document.createAttributeNS('urn:attribute', 'a:Flag');
namespaced.value = 'namespace';
console.log(namespaced.name, namespaced.localName, namespaced.prefix, namespaced.namespaceURI);
console.log(svg.setAttributeNodeNS(namespaced), svg.getAttributeNodeNS('urn:attribute', 'Flag') === namespaced);
console.log(svg.attributes.getNamedItemNS('urn:attribute', 'Flag') === namespaced, svg.getAttributeNS('urn:attribute', 'Flag'));
namespaced.value = 'updated';
console.log(svg.getAttributeNS('urn:attribute', 'Flag'), svg.getAttributeNode('a:Flag') === namespaced);
console.log(svg.attributes.removeNamedItemNS('urn:attribute', 'Flag') === namespaced, namespaced.ownerElement);
console.log(svg.attributes.setNamedItemNS(namespaced), svg.removeAttributeNode(namespaced) === namespaced);
svg.setAttributeNode(namespaced);
svg.setAttributeNS('urn:attribute', 'other:Flag', 'namespace-write');
console.log(namespaced.name, namespaced.value, svg.getAttributeNodeNS('urn:attribute', 'Flag') === namespaced);
svg.setAttribute('a:Flag', 'ordinary-write');
console.log(namespaced.namespaceURI, namespaced.value, svg.getAttributeNS('urn:attribute', 'Flag'));
svg.removeAttributeNS('urn:attribute', 'Flag');
console.log(namespaced.ownerElement, namespaced.value);

var foreign = document.implementation.createHTMLDocument('foreign');
var foreignOwner = foreign.createElement('section');
console.log(foreignOwner.setAttributeNode(replacement), replacement.ownerDocument === foreign, replacement.ownerElement === foreignOwner);
console.log(foreignOwner.getAttributeNode('data-value') === replacement);
foreignOwner.removeAttributeNode(replacement);
console.log(owner.setAttributeNode(replacement) !== null, replacement.ownerDocument === document, replacement.value);

for (var invalid of [{}, owner, document.createTextNode('text')]) {
    try { owner.setAttributeNode(invalid); } catch (error) { console.log(error.name); }
}
for (var invalidName of ['', 'bad name', 'bad/name', 'bad=name', 'bad>name', 'bad\u0000name']) {
    try { document.createAttribute(invalidName); } catch (error) { console.log(error.name); }
}
for (var pair of [[null, 'a:name'], ['urn:wrong', 'xml:name'], ['urn:wrong', 'xmlns:name'], ['http://www.w3.org/2000/xmlns/', 'name']]) {
    try { document.createAttributeNS(pair[0], pair[1]); } catch (error) { console.log(error.name); }
}
for (var call of [function() { owner.getAttributeNode(); }, function() { owner.setAttributeNode(); },
    function() { document.createAttribute(); }, function() { document.createAttributeNS('urn:test'); }]) {
    try { call(); } catch (error) { console.log(error.name); }
}
try { valueDescriptor.get.call(owner); } catch (error) { console.log(error.name); }
try { valueDescriptor.set.call(Object.create(Attr.prototype), 'bad'); } catch (error) { console.log(error.name); }
console.log(document.createAttribute({toString: function() { return 'COERCED'; }}).name);
var xmlDocument = document.implementation.createDocument(null, 'root', null);
console.log(xmlDocument.createAttribute('MixedCase').name, document.createAttributeNS(null, 'MixedCase').name);
var cookieDescriptor = Object.getOwnPropertyDescriptor(Document.prototype, 'cookie');
console.log(typeof cookieDescriptor.get, typeof cookieDescriptor.set, cookieDescriptor.enumerable, cookieDescriptor.configurable);
console.log(cookieDescriptor.get.call(document) === document.cookie);
try { cookieDescriptor.get.call(owner); } catch (error) { console.log(error.name); }
var innerDescriptor = Object.getOwnPropertyDescriptor(Element.prototype, 'innerHTML');
console.log(typeof innerDescriptor.get, typeof innerDescriptor.set, innerDescriptor.enumerable, innerDescriptor.configurable);
var markup = document.createElement('section');
innerDescriptor.set.call(markup, '<b data-value="markup">Text</b>');
console.log(innerDescriptor.get.call(markup), markup.firstChild.tagName);
try { innerDescriptor.set.call(attribute, 'bad'); } catch (error) { console.log(error.name); }

// native Attr writes must use the same mutation records as Element writes.
var observed = document.createElement('aside');
observed.setAttribute('data-observed', 'initial');
var observer = new MutationObserver(function() {});
observer.observe(observed, {attributes: true, attributeOldValue: true});
var observedAttribute = observed.getAttributeNode('data-observed');
observedAttribute.value = 'first';
var observedReplacement = document.createAttribute('data-observed');
observedReplacement.value = 'second';
observed.setAttributeNode(observedReplacement);
var records = observer.takeRecords();
console.log(records.length, records[0].attributeName, records[0].oldValue, records[1].oldValue);
observer.disconnect();

// retaining only the Attr must keep its detached owner alive through precise GC.
function retainedAttribute() {
    var detached = document.createElement('article');
    detached.setAttribute('data-retained', 'retained');
    return detached.getAttributeNode('data-retained');
}
var retained = retainedAttribute();
for (var i = 0; i < 30; i++) { var allocation = {value: 'allocate:' + i}; }
retained.value = 'after-collection';
console.log(retained.ownerElement.tagName, retained.ownerElement.getAttribute('data-retained'));
