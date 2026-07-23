let body = "return class Tune6Scaling {";
for (let i = 0; i < 130; i++) {
    body += "m" + i + "(){return " + i + ";}";
}
for (let i = 0; i < 20; i++) {
    body += "static s" + i + "=" + i + ";";
}
for (let i = 0; i < 40; i++) {
    body += "f" + i + "=" + i + ";";
}
for (let i = 0; i < 10; i++) {
    body += "static { this.b" + i + "=" + i + "; }";
}
body += "}";

let Scaling = Function(body)();
let value = new Scaling();
console.log(
    value.m0(),
    value.m129(),
    value.f39,
    Scaling.s19,
    Scaling.b9
);
