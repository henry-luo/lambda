(0, eval)(require('fs').readFileSync('test/js/tom-select.min.js', 'utf8'));

var tom = new TomSelect(document.getElementById('select'), {});
tom.setTextboxValue('Bet');
tom.refreshOptions(false);
tom.setValue('b');

console.log('tom-select:create:', tom instanceof TomSelect);
console.log('tom-select:value:', tom.getValue(), document.getElementById('select').value);
console.log('tom-select:selected:', document.getElementById('select').selectedOptions[0].textContent);
