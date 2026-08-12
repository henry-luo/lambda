// Tune5 exotic-operation lock: every exotic path keeps the operation and
// receiver explicit while ordinary shape storage remains the fallback.
let calls = [];
let target = { x: 1 };
let proxy = new Proxy(target, {
  get(t, key, receiver) { calls.push("g:" + key + ":" + (receiver === proxy)); return t[key]; },
  set(t, key, value, receiver) { calls.push("s:" + key + ":" + (receiver === proxy)); t[key] = value; return true; },
  has(t, key) { calls.push("h:" + key); return key in t; },
  getOwnPropertyDescriptor(t, key) { calls.push("o:" + key); return Object.getOwnPropertyDescriptor(t, key); },
  deleteProperty(t, key) { calls.push("d:" + key); return delete t[key]; },
  ownKeys(t) { calls.push("k"); return Reflect.ownKeys(t); }
});
console.log(Reflect.get(proxy, "x", proxy), Reflect.set(proxy, "x", 2, proxy),
            Reflect.has(proxy, "x"), Object.hasOwn(proxy, "x"),
            Reflect.deleteProperty(proxy, "x"), Reflect.ownKeys(proxy).join(","));
console.log(calls.join(","));

let typed = new Uint8Array([3, 4]);
console.log(typed[0], Reflect.set(typed, 1, 9), typed[1],
            Reflect.has(typed, "1"), Object.hasOwn(typed, "1"),
            Object.keys(typed).join(","));

function inspectArguments(first) {
  console.log(arguments[0], Reflect.has(arguments, "0"),
              Object.getOwnPropertyDescriptor(arguments, "0").value);
}
inspectArguments(7);
