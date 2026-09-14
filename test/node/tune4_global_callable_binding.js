function out(label, value) { console.log(label + ":" + value); }

let local = 1;
eval("local = 2");
out("direct", local);

const originalEval = eval;
globalThis.eval = function(source) { return "replacement:" + source; };
out("replaced", eval("local = 3"));
out("replacement-no-direct", local);

globalThis.eval = originalEval;
const alias = eval;
out("indirect", alias("1 + 4"));

let extra = 0;
out("extra-result", eval("6", extra = 7));
out("extra-effect", extra);
out("spread", eval(...["8 + 1", extra = 10]));
out("spread-effect", extra);

const originalNumber = Number;
globalThis.Number = function(value) { return "N:" + value; };
out("number-replaced", Number(4));
globalThis.Number = originalNumber;

const originalMath = Math;
globalThis.Math = { max() { return 91; } };
out("math-replaced", Math.max(1, 2));
globalThis.Math = originalMath;

const originalArray = Array;
globalThis.Array = function(value) { this.value = "A:" + value; return this; };
out("array-replaced-call", Array(12).value);
out("array-replaced-new", new Array(13).value);
globalThis.Array = originalArray;

const originalDate = Date;
globalThis.Date = function(value) { return "D:" + value; };
out("date-replaced", Date(14));
globalThis.Date = originalDate;

const originalSetTimeout = setTimeout;
globalThis.setTimeout = function(fn, delay) { return "T:" + delay + ":" + fn(); };
out("timer-replaced", setTimeout(function() { return 15; }, 16));
globalThis.setTimeout = originalSetTimeout;

try {
    eval("local = 11; throw new Error('eval-boom')");
} catch (error) {
    out("eval-throw", error.message + ":" + local);
}

// Tune4 startup keeps these bindings as real own slots while deferring each
// large namespace transaction until Get observes its lazy value.
out("lazy-own", ["process", "Buffer", "crypto"].every(
    name => Object.prototype.hasOwnProperty.call(globalThis, name)));
out("lazy-process-descriptor",
    Object.getOwnPropertyDescriptor(globalThis, "process").value === process);
out("lazy-buffer-descriptor",
    Object.getOwnPropertyDescriptor(globalThis, "Buffer").value === Buffer);
out("lazy-crypto-descriptor",
    Object.getOwnPropertyDescriptor(globalThis, "crypto").value === crypto);
out("lazy-buffer-module", Buffer === require("buffer").Buffer);
out("lazy-crypto-module", crypto === require("crypto"));

const originalBuffer = Buffer;
globalThis.Buffer = 42;
out("lazy-replaced", globalThis.Buffer);
globalThis.Buffer = originalBuffer;

const originalCrypto = crypto;
delete globalThis.crypto;
out("lazy-deleted", "crypto" in globalThis);
globalThis.crypto = originalCrypto;
