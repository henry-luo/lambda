// D8.4.3: ordinary uninitialized top-level vars use one bulk initialization
// call. A disabled batch path expands this fixture back into scalar calls.
var alpha, beta, gamma, delta, epsilon, zeta, eta, theta;
if (alpha !== undefined || beta !== undefined || gamma !== undefined ||
    delta !== undefined || epsilon !== undefined || zeta !== undefined ||
    eta !== undefined || theta !== undefined) {
  throw new Error("uninitialized var was not undefined");
}
console.log("module-var-bulk-ok");
