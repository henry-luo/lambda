var parent = document.getElementById('parent');
var old = document.getElementById('old');
var method = Element.prototype.replaceChildren;
console.log(typeof method === 'function' && typeof Document.prototype.replaceChildren === 'function');
var fragment = document.createDocumentFragment();
console.log(typeof fragment.replaceChildren === 'function' && typeof DocumentFragment.prototype.append === 'function');
var first = document.createElement('i');
first.textContent = 'first';
fragment.append(first, 'tail');
var observer = new MutationObserver(function () {});
observer.observe(parent, {childList:true, subtree:true, characterData:true});
parent.replaceChildren('lead', fragment, first);
console.log(parent.textContent === 'leadtailfirst' && fragment.childNodes.length === 0);
console.log(parent.lastChild === first && old.parentNode === null);
var records = observer.takeRecords();
console.log(records.length === 1 && records[0].target === parent && records[0].type === 'childList');
console.log(records[0].addedNodes.length === 3 && records[0].removedNodes.length === 1 && records[0].removedNodes[0] === old);
console.log(records[0].previousSibling === null && records[0].nextSibling === null);
old.firstChild.data = 'detached';
console.log(observer.takeRecords().length === 1);
var text = parent.firstChild;
try { parent.replaceChildren('changed', {toString:function(){throw new Error('convert');}}); console.log(false); }
catch (error) { console.log(error.message === 'convert' && parent.firstChild === text && parent.textContent === 'leadtailfirst'); }
try { parent.replaceChildren(parent); console.log(false); }
catch (error) { console.log(error.name === 'HierarchyRequestError' && parent.firstChild === text); }
parent.replaceChildren(first);
console.log(parent.childNodes.length === 1 && parent.firstChild === first && parent.innerHTML === '<i>first</i>');
records = observer.takeRecords();
console.log(records.length === 1 && records[0].addedNodes[0] === first && records[0].removedNodes.length === 3);
console.log(parent.replaceChildren() === undefined && parent.childNodes.length === 0 && parent.innerHTML === '');
var foreign = document.implementation.createHTMLDocument('foreign');
var adopted = foreign.createElement('em');
adopted.textContent = 'adopted';
parent.replaceChildren(adopted);
console.log(adopted.ownerDocument === document && parent.firstChild === adopted);
DocumentFragment.prototype.replaceChildren.call(fragment, 'a', 'b');
console.log(fragment.textContent === 'ab' && fragment.childNodes.length === 2);
console.log(method.length === 0 && method.name === 'replaceChildren');
console.log(method !== DocumentFragment.prototype.replaceChildren && method !== Document.prototype.replaceChildren);
try { Object.getOwnPropertyDescriptor(Element.prototype, 'children').get.call(fragment); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
try { method.call(fragment, 'x'); console.log(false); }
catch (error) { console.log(error instanceof TypeError && fragment.textContent === 'ab'); }
try { method.call({}, 'x'); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
try { document.replaceChildren('text'); console.log(false); }
catch (error) { console.log(error.name === 'HierarchyRequestError' && document.documentElement.tagName === 'HTML'); }
observer.disconnect();
console.log('REPLACE_CHILDREN_DONE');
