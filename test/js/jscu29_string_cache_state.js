// Concatenation, URI and ASCII paths share one realm-owned string cache state.
const prefix = '%' + 'A';
const byte = prefix + 'F';
const encoded = encodeURIComponent('😀');
console.log(prefix, byte, encoded, decodeURIComponent(encoded), String.fromCharCode(65));
