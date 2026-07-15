var host = document.getElementById('host');
var child = document.getElementById('child');
var delivered = 0;
var observer = new MutationObserver(function (records, self) {
  delivered++;
  console.log(records.length);
  console.log(records[0].type + ':' + records[0].attributeName + ':' + records[0].oldValue);
  console.log(self === observer);
});
observer.observe(host, {
  attributes: true,
  attributeOldValue: true,
  characterData: true,
  characterDataOldValue: true,
  childList: true,
  subtree: true
});
child.setAttribute('data-state', 'one');
child.setAttribute('data-state', 'two');
child.firstChild.data = 'new';
host.appendChild(document.createElement('b'));
console.log(observer.takeRecords().length);
child.setAttribute('data-state', 'three');
console.log('MO_SYNC:' + delivered);
