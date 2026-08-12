// Tune5 receiver and eight-operation lock: D6.2.2v2, D8.4.3.
let proto = {
  get value() { return this.marker; },
  set value(v) { this.marker = v; }
};
let target = Object.create(proto);
let receiver = { marker: 11 };
console.log(Reflect.get(target, "value", receiver));
console.log(Reflect.set(target, "value", 22, receiver), receiver.marker,
            Object.hasOwn(target, "value"));
console.log(Reflect.defineProperty(target, "own", {
  value: 31, writable: true, enumerable: true, configurable: true
}));
console.log(Reflect.getOwnPropertyDescriptor(target, "own").value);
console.log(Reflect.has(target, "value"), Reflect.has(target, "missing"));
console.log(Object.hasOwn(target, "own"), Reflect.deleteProperty(target, "own"));
console.log(Reflect.ownKeys(target).join(","));

let calls = [];
let proxy = new Proxy({}, {
  get(t, key, recv) { calls.push("g:" + key + ":" + (recv === proxy)); return 1; },
  set(t, key, value, recv) { calls.push("s:" + key + ":" + (recv === proxy)); return true; },
  defineProperty(t, key, desc) { calls.push("d:" + key); return true; },
  deleteProperty(t, key) { calls.push("x:" + key); return true; },
  has(t, key) { calls.push("h:" + key); return true; },
  getOwnPropertyDescriptor(t, key) { calls.push("o:" + key); return undefined; },
  ownKeys(t) { calls.push("k"); return []; }
});
Reflect.get(proxy, "a", proxy);
Reflect.set(proxy, "b", 2, proxy);
Reflect.defineProperty(proxy, "c", { value: 3 });
Reflect.deleteProperty(proxy, "d");
Reflect.has(proxy, "e");
Object.hasOwn(proxy, "f");
Reflect.getOwnPropertyDescriptor(proxy, "g");
Reflect.ownKeys(proxy);
console.log(calls.join(","));
