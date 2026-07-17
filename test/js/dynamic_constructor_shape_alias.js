var defaultSetter = function (target, key, value) {
  target[key] = value;
};

function DynamicRecord(next, target, key, start, change, render, data, setter, priority) {
  this.target = target;
  this.start = start;
  this.change = change;
  this.key = key;
  this.render = render || defaultSetter;
  this.data = data || this;
  this.setter = setter || defaultSetter;
  this.priority = priority || 0;
  this.next = next;
}

var holder = { Constructor: DynamicRecord };
var Alias = holder.Constructor;
var record = new Alias(null, { opacity: 1 }, 'opacity', 1, -0.5);
console.log(record.key + ':' + record.start + ':' + record.change + ':' + record.priority);
console.log(typeof record.render + ':' + typeof record.setter + ':' + (record.data === record));
