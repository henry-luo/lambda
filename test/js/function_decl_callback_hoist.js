let value = 41;

setTimeout(later, 0);

function later() {
  console.log("function-callback-hoist:", value + 1);
}
