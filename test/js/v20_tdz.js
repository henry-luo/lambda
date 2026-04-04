// TDZ (Temporal Dead Zone) enforcement tests

// Test 1: let variable in TDZ throws ReferenceError
function test_let_tdz() {
  try {
    var result = x;
    return "FAIL";
  } catch (e) {
    if (e.name !== "ReferenceError") return "FAIL: wrong error " + e.name;
  }
  let x = 5;
  if (x !== 5) return "FAIL: expected 5";
  return "PASS";
}
console.log(test_let_tdz());

// Test 2: const variable in TDZ throws ReferenceError
function test_const_tdz() {
  try {
    var result = y;
    return "FAIL";
  } catch (e) {
    if (e.name !== "ReferenceError") return "FAIL: wrong error " + e.name;
  }
  const y = 10;
  if (y !== 10) return "FAIL: expected 10";
  return "PASS";
}
console.log(test_const_tdz());

// Test 3: let without initializer still enforces TDZ
function test_let_no_init() {
  try {
    var result = v;
    return "FAIL";
  } catch (e) {
    if (e.name !== "ReferenceError") return "FAIL: wrong error " + e.name;
  }
  let v;
  if (v !== undefined) return "FAIL: expected undefined, got " + v;
  return "PASS";
}
console.log(test_let_no_init());

// Test 4: var hoisting does NOT throw (var is NOT TDZ)
function test_var_no_tdz() {
  var result = a;
  var a = 5;
  if (a !== 5) return "FAIL: expected 5";
  return "PASS";
}
console.log(test_var_no_tdz());

// Test 5: TDZ error message includes variable name
function test_tdz_message() {
  try {
    var result = myVar;
    return "FAIL";
  } catch (e) {
    if (e.message.indexOf("myVar") === -1) return "FAIL: message doesn't mention myVar: " + e.message;
    return "PASS";
  }
  let myVar = 1;
}
console.log(test_tdz_message());

// Test 6: let is accessible after declaration in same function
function test_let_after_decl() {
  let a = 1;
  let b = 2;
  return a + b;
}
console.log(test_let_after_decl());
