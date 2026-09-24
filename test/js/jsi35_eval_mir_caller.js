// JSI35: MIR-compiled callers hand direct eval to the AST interpreter through
// the EvalContext bridge. Pinned to MIR by test/js/mir_list.txt.
let topLet = 10;
const topConst = 20;
var topVar = 30;

function readTop() { return eval("topLet + topConst + topVar"); }
console.log("read-top:", readTop());

function writeTopLet() { eval("topLet = 11"); return topLet; }
console.log("write-top-let:", writeTopLet(), topLet);

function shadowedTop() { let topLet = 99; return eval("topLet"); }
console.log("shadowed-top:", shadowedTop());

function locals(a) { var b = 2; eval("a = a + b; var c = 3"); return a + ":" + c; }
console.log("locals:", locals(1));

function completion() { return eval("var q = 1; if (q) { 'yes' } else { 'no' }"); }
console.log("completion:", completion());

try { (function () { "use strict"; const k = 1; eval("k = 2"); })(); }
catch (e) { console.log("strict-const:", e.name); }

console.log("indirect:", (0, eval)("typeof topVar"));
console.log("function-ctor:", Function("return typeof topVar")());

// global eval code reads a MIR script's top-level lexicals through the realm
// record, which links their module slots
function globalReaders() { return (0, eval)("topLet + topConst") + ":" + Function("return topConst")(); }
console.log("global-readers:", globalReaders());
