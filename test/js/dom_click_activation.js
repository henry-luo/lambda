function check(name, condition) {
  if (!condition) throw new Error('Click activation: ' + name);
  console.log(name + ': pass');
}
function input(type, name) {
  var node = document.createElement('input');
  node.type = type;
  if (name) node.name = name;
  document.body.appendChild(node);
  return node;
}
var checked = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'checked');
check('checked accessor', typeof checked.get === 'function' && typeof checked.set === 'function');
try {
  checked.get.call(document.createElement('div'));
  check('receiver validation', false);
} catch (error) { check('receiver validation', error instanceof TypeError); }
var control = input('checkbox');
var events = [];
control.indeterminate = true;
control.addEventListener('click', function(event) {
  events.push('click:' + control.checked + ':' + control.indeterminate);
  event.preventDefault();
}, {once: true});
control.addEventListener('input', function() { events.push('input:' + control.checked); });
control.addEventListener('change', function() { events.push('change:' + control.checked); });
var canceled = !control.dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true}));
check('checkbox canceled preactivation', canceled && !control.checked && control.indeterminate && events.join(',') === 'click:true:false');
events.length = 0;
control.click();
check('dynamic checkbox commit', control.checked && !control.indeterminate && events.join(',') === 'input:true,change:true');

var first = input('radio', 'activation');
var second = input('radio', 'activation');
first.checked = true;
var sawRadio = false;
second.addEventListener('click', function(event) {
  sawRadio = !first.checked && second.checked;
  if (typeof gc === 'function') gc();
  event.preventDefault();
}, {once: true});
second.click();
check('radio canceled preactivation', sawRadio && first.checked && !second.checked);
second.click();
check('dynamic radio commit', !first.checked && second.checked);
first.addEventListener('click', function(event) {
  second.name = 'another-group';
  event.preventDefault();
}, {once: true});
first.click();
check('radio changed-group cancellation', !first.checked && !second.checked);

var nested = input('checkbox');
var depth = 0;
var states = [];
nested.addEventListener('click', function(event) {
  states.push(nested.checked);
  if (depth === 0) {
    depth++;
    nested.dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true}));
    states.push(nested.checked);
  }
  event.preventDefault();
});
nested.dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true}));
check('nested cancellation snapshots', !nested.checked && states.join(',') === 'true,false,true');
control.remove(); first.remove(); second.remove(); nested.remove();
if (typeof gc === 'function') gc();
console.log('CLICK_ACTIVATION_DONE');
