var target = document.getElementById('target');
var observer = new MutationObserver(function () {});

observer.observe(target, {attributeFilter: ['data-ready']});
target.setAttribute('data-other', 'ignored');
target.setAttribute('data-ready', 'set');

var records = observer.takeRecords();
console.log(records.length);
console.log(records[0].attributeName);
