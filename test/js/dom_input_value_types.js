var input = document.getElementById('input');

input.type = 'date';
input.value = '2024-02-29';
console.log('date:', input.type, input.value, input.valueAsNumber, input.valueAsDate.toISOString());
input.value = '2023-02-29';
console.log('date-invalid:', input.value === '', Number.isNaN(input.valueAsNumber));
input.valueAsNumber = 0;
console.log('date-number:', input.value);
input.min = '1970-01-02';
input.max = '1970-01-10';
input.step = '2';
console.log('date-validity:', input.validity.rangeUnderflow, input.validity.rangeOverflow, input.validity.stepMismatch);
input.value = '1970-01-04';
console.log('date-step-valid:', input.validity.valid);

input.type = 'month';
input.removeAttribute('min');
input.removeAttribute('max');
input.removeAttribute('step');
input.value = '2025-07';
console.log('month:', input.value, input.valueAsDate.toISOString());
input.stepUp(2);
console.log('month-step:', input.value);

input.type = 'week';
input.value = '2020-W53';
console.log('week:', input.value, input.valueAsDate.toISOString());
input.value = '2021-W53';
console.log('week-invalid:', input.value === '');

input.type = 'time';
input.value = '23:59:58.125';
console.log('time:', input.value, input.valueAsNumber, input.valueAsDate.toISOString());
input.valueAsNumber = 60000;
console.log('time-number:', input.value);

input.type = 'datetime-local';
input.value = '2024-01-02 03:04';
console.log('datetime:', input.value, input.valueAsDate === null);

input.type = 'color';
input.value = '#Aa00Ff';
console.log('color:', input.value);
input.value = 'red';
console.log('color-invalid:', input.value);

input.type = 'made-up';
console.log('unknown-type:', input.type);

input.type = 'file';
var transfer = new DataTransfer();
transfer.items.add(new File(['abc'], 'sample.txt', {type: 'text/plain'}));
input.files = transfer.files;
var data = new FormData(document.getElementById('form'));
console.log('file:', input.files.length, input.files[0].name, input.value, data.get('upload').name);
console.log('file-list:', input.files instanceof FileList, input.files.item(0).name, Object.prototype.toString.call(input.files));
try {
    input.files = [];
} catch (error) {
    console.log('file-list-brand:', error.name);
}
