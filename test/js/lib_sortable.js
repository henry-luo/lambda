// @document dom2_library_probe.html
(0, eval)(require('fs').readFileSync('test/js/sortable.min.js', 'utf8'));

var sortableEnd = '';
var sortable = Sortable.create(document.getElementById('sortable'), {
  dataIdAttr: 'data-id',
  onEnd: function (event) {
    sortableEnd = event.oldIndex + '>' + event.newIndex;
  }
});
sortable.sort(['three', 'one', 'two']);
sortable.options.onEnd({ oldIndex: 0, newIndex: 2 });

console.log('sortable:create:', sortable instanceof Sortable);
console.log('sortable:order:', sortable.toArray().join(','));
console.log('sortable:end:', sortableEnd);
