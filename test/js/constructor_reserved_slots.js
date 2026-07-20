function BoundMethod() {
  console.log('bound-prototype:' + (Object.getPrototypeOf(this) === BoundMethod.prototype));
  console.log('bound-before-own:' + Object.prototype.hasOwnProperty.call(this, 'run'));
  console.log('bound-types:' + typeof this.run + ':' + typeof BoundMethod.prototype.run);
  console.log('bound-before-proto:' + (this.run === BoundMethod.prototype.run));
  this.run = this.run.bind(this);
  console.log('bound-after-own:' + Object.prototype.hasOwnProperty.call(this, 'run'));
}

BoundMethod.prototype.run = function () { return this; };
const bound = new BoundMethod();
console.log('bound-call:' + (bound.run() === bound));

class ClassBoundMethod {
  constructor() {
    console.log('class-before-own:' + Object.prototype.hasOwnProperty.call(this, 'run'));
    console.log('class-before-proto:' + (this.run === ClassBoundMethod.prototype.run));
    this.run = this.run.bind(this);
  }
  run() { return this; }
}
const classBound = new ClassBoundMethod();
console.log('class-after-own:' + Object.prototype.hasOwnProperty.call(classBound, 'run'));
console.log('class-call:' + (classBound.run() === classBound));

class LargeBoundMethod {
  constructor(config = {}) {
    this.p0 = 0;
    this.p1 = 1;
    this.p2 = 2;
    this.p3 = 3;
    this.p4 = 4;
    this.p5 = 5;
    this.p6 = 6;
    this.p7 = 7;
    this.p8 = 8;
    this.p9 = 9;
    this.p10 = 10;
    this.p11 = 11;
    this.p12 = 12;
    this.p13 = 13;
    const { dispatch } = config;
    this.dispatchTransactions = dispatch || (transactions => this.update(transactions));
    this.dispatch = this.dispatch.bind(this);
  }
  dispatch(...transactions) { this.dispatchTransactions(transactions); }
  update(transactions) { this.last = transactions; }
}
const largeBound = new LargeBoundMethod();
largeBound.dispatch('large');
console.log('large-bound:' + largeBound.last[0]);

function PrototypeSetter() {
  this.value = 7;
  console.log('setter-own:' + Object.prototype.hasOwnProperty.call(this, 'value'));
  console.log('setter-seen:' + this.seen);
}

Object.defineProperty(PrototypeSetter.prototype, 'value', {
  get: function () { return 3; },
  set: function (value) { this.seen = value; },
  configurable: true
});
new PrototypeSetter();

function ReflectionOrder() {
  console.log('keys-before:' + Object.keys(this).join(','));
  this.first = 1;
  console.log('keys-after:' + Object.keys(this).join(','));
}
new ReflectionOrder();
