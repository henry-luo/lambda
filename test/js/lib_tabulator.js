(0, eval)(require('fs').readFileSync('test/js/dom2_library_probe.js', 'utf8'));
dom2_probe('tabulator', 'test/js/tabulator.min.js', function () {
  return !!new Tabulator(document.getElementById('table'), { data: [{ id: 1, name: 'one' }], columns: [{ title: 'Name', field: 'name' }] });
});
