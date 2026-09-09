// JSCU29: tagged-template identity uses dynamic native rows and one RootVector.
function tag(strings) {
    return strings;
}

function build() {
    return tag`lambda`;
}

var first = build();
gc();
var second = build();
console.log(first === second, first.raw[0], Object.isFrozen(first),
    Object.isFrozen(first.raw));
