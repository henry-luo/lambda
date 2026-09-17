const values = ['alpha', 'beta'].values();
console.log(values.next().value);
console.log(values.next().value);
console.log(values.next().done);

const keys = ['alpha'].keys();
console.log(keys.next().value);

const entries = ['alpha'].entries();
console.log(entries.next().value.join(':'));
