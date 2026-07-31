class Left {
  #value = 1;
  get() { return this.#value; }
  has(object) { return #value in object; }
}

class Right {
  #value = 2;
  get() { return this.#value; }
}

let left = new Left();
let right = new Right();
console.log("classes:" + left.get() + "," + right.get() + "," + left.has(right));

function makeCounter() {
  return class {
    #value = 7;
    get() { return this.#value; }
    has(object) { return #value in object; }
  };
}

let First = makeCounter();
let Second = makeCounter();
let first = new First();
let second = new Second();
console.log("reeval:" + first.get() + "," + second.get() + "," + first.has(second));

let ordinary = {};
ordinary["__private_value"] = "public";
console.log("public:" + ordinary["__private_value"]);

class EvalBox {
  #value = 9;
  read() { return eval("this.#value"); }
}
console.log("eval:" + new EvalBox().read());

class StaticBox {
  static #value = 12;
  static read() { return this.#value; }
}
console.log("static:" + StaticBox.read());

class AccessorBox {
  #value = 15;
  get #secret() { return this.#value; }
  read() { return this.#secret; }
}
console.log("accessor:" + new AccessorBox().read());

class FieldInitializerBox {
  #suffix() { return "!"; }
  value = this.#suffix();
  static #staticSuffix() { return "?"; }
  static value = this.#staticSuffix();
}
console.log("field-home:" + new FieldInitializerBox().value + "," + FieldInitializerBox.value);

class FieldBase {}
class DerivedFieldInitializerBox extends FieldBase {
  #suffix() { return "derived"; }
  value = this.#suffix();
  constructor() { super(); }
}
console.log("derived-field-home:" + new DerivedFieldInitializerBox().value);
