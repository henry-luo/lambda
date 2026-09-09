// JSCU35: methods, fields, and static blocks share one source-ordered table.
let trace = [];
let key = value => { trace.push(value); return value; };

class Base {
    #base = 1;
    value() { return this.#base; }
}

class Mixed extends Base {
    static [key("static-key-a")] = (trace.push("static-init-a"), 1);
    static { trace.push("static-block"); }
    static [key("static-key-b")] = (trace.push("static-init-b"), 2);
    [key("instance-key")] = 3;
    #own = 4;
    value() { return this.#own + super.value() + this["instance-key"]; }
}

console.log(trace.join(","));
console.log(Mixed["static-key-a"] + Mixed["static-key-b"]);
console.log(new Mixed().value());
