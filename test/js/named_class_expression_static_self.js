var PublicName = class PrivateName {
    static self() {
        return PrivateName;
    }

    static nestedSelf() {
        return (() => PrivateName)();
    }
};

console.log(PublicName.self() === PublicName);
console.log(PublicName.nestedSelf() === PublicName);

function Base(value) {
    this.value = value;
}

var Derived = class PrivateDerived extends Base {
    constructor(value) {
        super(value);
    }
};

console.log(new Derived('dynamic-super').value);
