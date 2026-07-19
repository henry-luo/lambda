// @document dom2_library_probe.html
(0, eval)(require('fs').readFileSync('test/js/flatpickr.min.js', 'utf8'));

var flatpickrChanges = 0;
var calendar = flatpickr(document.getElementById('date-input'), {
  dateFormat: 'Y-m-d',
  onChange: function () { flatpickrChanges++; }
});
calendar.open();
calendar.setDate('2024-03-14', true);

console.log('flatpickr:open:', calendar.isOpen);
console.log('flatpickr:value:', calendar.input.value);
console.log('flatpickr:change:', flatpickrChanges, calendar.selectedDates.length);
calendar.close();
