// D1.3/D8.4.1v2: immutable literal recipes use an exact shape and raw slot
// read; a type transition, deletion, or accessor definition takes NameId miss.
function literalFields() {
  const point = { x: 7.5, label: "field" };
  return point.x + ":" + point.label;
}

function immediateField() {
  return ({ value: 4.5 }).value;
}

function makeReturnedPoint() {
  return { x: 3.5, label: "returned" };
}

function returnedFields() {
  return makeReturnedPoint().x + ":" + makeReturnedPoint().label;
}

function consumeDirectArgument(point) {
  return point.x + ":" + point.label;
}

function directArgumentFields() {
  return consumeDirectArgument({ x: 5.5, label: "argument" });
}

function directStore() {
  const point = { x: 1.5 };
  point.x = 2.5;
  return point.x;
}

function typeTransitionFallback() {
  const point = { x: 1.5 };
  point.x = "changed";
  return point.x;
}

function deleteFallback() {
  const point = { x: 1.5 };
  delete point.x;
  return point.x;
}

function frozenStoreFallback() {
  const point = { x: 1.5 };
  Object.freeze(point);
  point.x = 2.5;
  return point.x;
}

function accessorFallback() {
  const point = { x: 1.5 };
  let stored = "accessor";
  Object.defineProperty(point, "x", {
    get: function() { return stored; }, set: function(value) { stored = value; }, configurable: true
  });
  point.x = "setter";
  return point.x;
}

class ClassFieldPlan {
  x = 6.5;
  label = "class";

  read() {
    return this.x + ":" + this.label;
  }

  store() {
    this.x = 8.5;
    return this.x;
  }
}

function classFields() {
  const point = new ClassFieldPlan();
  return point.read() + ":" + point.store();
}

function classAccessorEscapeFallback() {
  const point = new ClassFieldPlan();
  Object.defineProperty(point, "x", {
    get: function() { return "escaped"; }, configurable: true
  });
  return point.read();
}

if (literalFields() !== "7.5:field" || immediateField() !== 4.5 ||
    returnedFields() !== "3.5:returned" || directStore() !== 2.5 ||
    directArgumentFields() !== "5.5:argument" ||
    typeTransitionFallback() !== "changed" || deleteFallback() !== undefined ||
    frozenStoreFallback() !== 1.5 || accessorFallback() !== "setter" ||
    classFields() !== "6.5:class:8.5" ||
    classAccessorEscapeFallback() !== "escaped:class") {
  throw new Error("native field plan changed semantics");
}
console.log("native-field-plan-ok");
