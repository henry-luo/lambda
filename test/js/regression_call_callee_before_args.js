let selected = () => "callee";
let result = selected(selected = () => "argument");
console.log(result + ":" + selected());
