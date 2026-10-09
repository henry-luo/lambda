"use strict";

function check(condition, message) {
    if (!condition) throw new Error(message);
}
async function rejects(action, name) {
    let caught = false;
    try { await action(); } catch (error) { caught = error.name === name; }
    check(caught, "expected " + name);
}

async function run() {
    const json = Response.prototype.json;
    const text = Response.prototype.text;
    const status = Object.getOwnPropertyDescriptor(Response.prototype, "status").get;
    const used = Object.getOwnPropertyDescriptor(Response.prototype, "bodyUsed").get;
    const response = new Response('{"value":17}', {
        status: 201, statusText: "Created", headers: {"x-test": "yes"}
    });
    check(response instanceof Response && response.ok && response.status === 201 &&
        response.statusText === "Created" && response.type === "default" &&
        response.url === "" && response.headers instanceof Headers &&
        response.headers.get("x-test") === "yes" &&
        response.headers.get("content-type") === "text/plain;charset=UTF-8" &&
        !Object.hasOwn(response, "json") && !Object.hasOwn(response, "status"),
        "Response metadata or declaring prototype changed");
    const clone = response.clone();
    response.headers.set("x-test", "changed");
    check(clone.headers.get("x-test") === "yes", "clone shares mutable headers");
    const privateClone = new Response("private");
    privateClone.headers[Symbol.iterator] = () => { throw new Error("public iterator"); };
    check(await privateClone.clone().text() === "private", "clone used the public Headers iterator");
    response.__body_idx = -1;
    Object.setPrototypeOf(response, null);
    check(status.call(response) === 201 && !used.call(response), "captured getter lost its brand");
    check((await json.call(response)).value === 17 && used.call(response),
        "captured method lost its private body");
    await rejects(() => text.call(response), "TypeError");
    check((await clone.json()).value === 17, "clone consumption changed");
    await rejects(() => clone.clone(), "TypeError");
    await rejects(() => text.call(Object.create(Response.prototype)), "TypeError");
    await rejects(() => status.call({}), "TypeError");

    const source = new Uint8Array([0, 255, 65]);
    const binary = new Response(source);
    source[1] = 0;
    const binaryClone = binary.clone();
    const buffer = await binary.arrayBuffer();
    const copy = new Uint8Array(buffer);
    check(copy[0] === 0 && copy[1] === 255 && copy[2] === 65 && binary.bodyUsed,
        "Response did not snapshot binary input");
    const bytes = await binaryClone.bytes();
    check(bytes instanceof Uint8Array && bytes[1] === 255, "bytes did not return a Uint8Array");
    const decoded = await new Response(new Uint8Array([239, 187, 191, 65, 255])).text();
    check(decoded === "A\ufffd", "Response text did not decode UTF-8");
    check(await new Response("\ud800").text() === "\ufffd", "Response string was not a USVString");
    const blob = await new Response("blob", {headers: {"content-type": "text/plain"}}).blob();
    check(blob instanceof Blob && blob.type === "text/plain" && await blob.text() === "blob",
        "Response did not create a native Blob");

    const invalid = new Response("{");
    await rejects(() => invalid.json(), "SyntaxError");
    check(invalid.bodyUsed, "failed JSON parsing did not consume its body");
    const empty = new Response();
    check(await empty.text() === "" && await empty.text() === "" && !empty.bodyUsed,
        "null body was marked consumed");
    await rejects(() => new Response("body", {status: 204}), "TypeError");
    await rejects(() => new Response(null, {status: 199}), "RangeError");
    await rejects(() => new Response(null, {statusText: "bad\nphrase"}), "TypeError");
    await rejects(() => new Response(null, {statusText: "\u0100"}), "TypeError");
    await rejects(() => Response("body"), "TypeError");
    class Derived extends Response {}
    const derived = new Derived("derived");
    check(derived instanceof Derived && await derived.text() === "derived",
        "Response lost the newTarget prototype");
    console.log("Response prototype, body ownership, and consumption passed");
}
run().catch(error => { console.log("FAILED", error.name, error.message); });
