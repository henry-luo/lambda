(0, eval)(require('fs').readFileSync('test/js/tabulator.min.js', 'utf8'));

var rows = [];
for (var id = 40; id >= 1; id--) {
  rows.push({ id: id, name: 'row-' + id });
}

var table = new Tabulator(document.getElementById('table'), {
  height: 180,
  rowHeight: 30,
  data: rows,
  columns: [
    { title: 'Id', field: 'id', sorter: 'number' },
    { title: 'Name', field: 'name' }
  ]
});

table.on('tableBuilt', function () {
  console.log('tabulator:built:' + table.getData().length);

  document.querySelector('.tabulator-col[tabulator-field="id"]')
    .dispatchEvent(new MouseEvent('click', { bubbles: true }));
  console.log('tabulator:sort:' + table.getData('active')[0].id);

  var holder = document.querySelector('.tabulator-tableholder');
  holder.scrollTop = 600;
  holder.dispatchEvent(new Event('scroll', { bubbles: true }));
  var firstName = document.querySelector(
    '.tabulator-row .tabulator-cell[tabulator-field="name"]'
  );
  console.log('tabulator:scroll:' + holder.scrollTop + ':' + firstName.textContent);
});
