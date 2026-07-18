class Operation {
  constructor(prefix) {
    this.prefix = prefix;
  }

  apply(value) {
    return this.prefix + value;
  }

  call(left, right) {
    return this.prefix + left + right;
  }
}

const operation = new Operation('ok:');
console.log(operation.apply('apply'));
console.log(operation.call('ca', 'll'));
console.log(function (value) { return this.prefix + value; }.apply(operation, ['function']));
console.log(function (left, right) { return this.prefix + left + right; }.call(operation, 'fun', 'ction'));
