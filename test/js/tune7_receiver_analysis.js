// Tune7 C3.0: parameter defaults participate in the existing receiver
// analysis, and an oblivious callee must consume a pending new.target.
var receiver = {
    value: 12,
    readDefault: function(value = this.value) { return value; }
};
console.log(receiver.readDefault());

var defaultNewTarget = 0;
function readNewTarget(value = new.target ? 7 : 3) {
    defaultNewTarget = value;
}
new readNewTarget();
console.log(defaultNewTarget);

function evalReadsThis() { return eval("this.value"); }
console.log(evalReadsThis.call({ value: 21 }));

function arrowFactory() { return () => this.value; }
var arrow = arrowFactory.call({ value: 34 });
console.log(arrow());

var observedNewTarget = 0;
function oblivious() { return 1; }
function observeNext() { observedNewTarget = new.target ? 1 : 2; }
Reflect.construct(oblivious, []);
observeNext();
console.log(observedNewTarget);
