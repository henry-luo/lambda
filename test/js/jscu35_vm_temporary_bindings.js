// JSCU35: VM temporary aliases and context extensions share an unbounded journal.
const vm = require("vm");
const context = vm.createContext({});
const extensions = [];
for (let i = 0; i < 17; i++) {
    extensions.push({ value: i });
}
const compiled = vm.compileFunction("return value;", undefined, {
    parsingContext: context,
    contextExtensions: extensions,
});
console.log(compiled());
