function run() {
    class Holder {
        constructor() {
            this.value = lateValue + otherValue;
        }
    }
    var lateValue = 40;
    var otherValue = 2;
    return new Holder().value;
}

console.log("class-hoist:", run());
