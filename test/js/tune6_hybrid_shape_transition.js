class HybridShape {
  constructor() {
    this.fixedFlag = false;
    this.fixedValue = 7;
  }
}

var value = new HybridShape();
for (var i = 0; i < 20; i++) {
  value["dynamic" + i] = i;
}
value.lateFlag = false;
value.afterFlag = 123456;
value.lateFlag = {answer: 42};

console.log(value.fixedFlag, value.fixedValue);
console.log(value.lateFlag.answer, value.afterFlag, value.dynamic19);
