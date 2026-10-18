__lambda_clipboard_write_records([{ "text/plain": "native record" }]);
const record = __lambda_clipboard_read_records()[0];
const hasOwn = Object.prototype.hasOwnProperty;

console.log(hasOwn.call(record, "text/plain"), hasOwn.call(record, "missing"));
console.log(hasOwn.call(record, "toString"), Object.hasOwn(record, "text/plain"));

let reads = 0;
Object.defineProperty(record, "accessor", {
  get() { reads++; return "value"; },
  configurable: true
});
console.log(hasOwn.call(record, "accessor"), reads);
const symbol = Symbol("native own");
record[symbol] = "symbol value";
console.log(hasOwn.call(record, symbol), hasOwn.call(record, String(symbol)));

const ordinary = Object.create({ inherited: true });
ordinary.own = true;
console.log(hasOwn.call(ordinary, "own"), hasOwn.call(ordinary, "inherited"));
