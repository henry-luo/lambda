// JSCU33(A): closures share immutable AST definition facts while retaining
// independent lexical environments and value identity.
function makeAdder(seed) {
  return function add(value) {
    return seed + value;
  };
}

const first = makeAdder(2);
const second = makeAdder(7);
const directEval = function(value) {
  return eval("value + 1");
};

console.log(first(5), second(5), first !== second, directEval(4));
