// Getter dispatch can allocate while resolving the inherited accessor. The
// receiver must remain an exact root through that nested call.
function readByteLength(view) {
    return view.byteLength;
}

const view = new DataView(new ArrayBuffer(4));
console.log(readByteLength(view));
console.log(view.byteOffset);
