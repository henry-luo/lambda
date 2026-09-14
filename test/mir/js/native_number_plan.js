// Tune11 T11-1: a statically proven JavaScript Number pair lowers to the
// shared F64 operation plan; a partial fact must retain the boxed helper.
function nativeNumberLoop() {
    let total = 0;
    for (let i = 0; i < 16; i++) total = total + 0.5;
    return total === 8;
}

function boolMutation() {
  let value = true;
  value = value + 0.5;
  return value === 1.5;
}

function partialNumber(value) {
    return value + 1;
}

if (!nativeNumberLoop() || !boolMutation() || partialNumber("n") !== "n1") {
    throw new Error("native Number plan changed JS semantics");
}
console.log("native-number-plan-ok");
