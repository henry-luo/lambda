function callMember(object) {
    return object.run(3);
}

function Callable(value) {
    this.value = value;
}

function constructWith(Ctor) {
    return new Ctor(4);
}

var receiver = {
    run: function (value) { return value + 1; }
};
console.log(callMember(receiver) + ":" + constructWith(Callable).value);
