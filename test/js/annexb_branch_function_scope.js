// Annex B block functions create an undefined outer binding before execution.
console.log(typeof ifBranchFunction);
if (true) function ifBranchFunction() { return "if"; }
console.log(ifBranchFunction());

console.log(typeof switchBranchFunction);
switch (1) {
case 1:
    function switchBranchFunction() { return "switch"; }
}
console.log(switchBranchFunction());
