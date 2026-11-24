const target = document.createElement('section');
document.body.appendChild(target);
target.innerHTML = '<b>old</b><i>stale</i>';
const range = document.createRange();
for (const word of ['first', 'second']) {
  range.selectNodeContents(target);
  range.deleteContents();
  const fresh = document.createElement('span');
  fresh.textContent = word;
  target.appendChild(fresh);
  console.log('replacement ' + word + ': ' + (target.textContent === word && target.children.length === 1));
}
const fragmentRange = document.createRange();
fragmentRange.selectNodeContents(target);
const fragment = fragmentRange.extractContents();
console.log('extract clears source: ' + (target.childNodes.length === 0));
const observer = new MutationObserver(() => {});
observer.observe(target, {childList: true});
target.appendChild(fragment);
const [insertion] = observer.takeRecords();
observer.disconnect();
console.log('fragment insertion record: ' + (
  insertion.addedNodes[0] === target.firstElementChild &&
  insertion.removedNodes instanceof NodeList && Array.from(insertion.removedNodes).length === 0
));
console.log('extract preserves identity: ' + (target.firstElementChild.textContent === 'second' && target.children.length === 1));
target.remove();
