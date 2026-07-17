// Node.getRootNode must bridge Radiant's implicit document root.

var connected = document.getElementById('connected-child');
console.log('root-node:connected:', connected.getRootNode() === document);
console.log('root-node:selector-domstring:',
  String(['span', 'button']),
  document.getElementById('connected').querySelectorAll(['span', 'button']).length,
  document.getElementById('connected').querySelectorAll('span,button').length);
console.log('root-node:composed:', connected.getRootNode({ composed: true }) === document);
console.log('root-node:document:', document.getRootNode() === document);
console.log('root-node:document-interface:', typeof Document, document instanceof Document);
console.log('root-node:html-interfaces:',
  document.createElement('form') instanceof HTMLFormElement,
  document.createElement('input') instanceof HTMLInputElement,
  document.createElement('a') instanceof HTMLAnchorElement);

var detachedRoot = document.createElement('section');
var detachedChild = document.createElement('span');
detachedRoot.appendChild(detachedChild);
console.log('root-node:detached:', detachedChild.getRootNode() === detachedRoot);

var fragment = document.createDocumentFragment();
var fragmentChild = document.createTextNode('fragment child');
fragment.appendChild(fragmentChild);
console.log('root-node:fragment:', fragmentChild.getRootNode() === fragment);

fragmentChild.libraryState = { active: true };
var comment = document.createComment('bookkeeping');
comment.libraryState = { active: false };
console.log('root-node:character-data-expandos:',
  fragmentChild.libraryState.active, comment.libraryState.active);

var parsed = new DOMParser().parseFromString('<span id="adopted"><b>owned</b></span>', 'text/html');
var adopted = parsed.body.firstChild;
document.getElementById('connected').appendChild(adopted);
console.log('root-node:cross-document-adoption:',
  adopted.ownerDocument === document,
  adopted.firstChild.ownerDocument === document,
  adopted.textContent);
