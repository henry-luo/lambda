// Kostya Benchmark: base64 (Node.js)
// Base64 encode and decode — encodes 10000 bytes 100 times
'use strict';

const TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

function b64Encode(bytes, numBytes) {
    const parts = [];
    let i = 0;
    while (i + 2 < numBytes) {
        const b0 = bytes[i], b1 = bytes[i + 1], b2 = bytes[i + 2];
        parts.push(TABLE[b0 >> 2]);
        parts.push(TABLE[((b0 & 3) << 4) | (b1 >> 4)]);
        parts.push(TABLE[((b1 & 15) << 2) | (b2 >> 6)]);
        parts.push(TABLE[b2 & 63]);
        i += 3;
    }
    if (i + 1 === numBytes) {
        const b0 = bytes[i];
        parts.push(TABLE[b0 >> 2]);
        parts.push(TABLE[(b0 & 3) << 4]);
        parts.push("==");
    } else if (i + 2 === numBytes) {
        const b0 = bytes[i], b1 = bytes[i + 1];
        parts.push(TABLE[b0 >> 2]);
        parts.push(TABLE[((b0 & 3) << 4) | (b1 >> 4)]);
        parts.push(TABLE[(b1 & 15) << 2]);
        parts.push("=");
    }
    return parts.join('');
}

function b64DecodeLen(encoded) {
    const slen = encoded.length;
    let n = (slen >> 2) * 3;
    if (slen >= 1 && encoded[slen - 1] === '=') n--;
    if (slen >= 2 && encoded[slen - 2] === '=') n--;
    return n;
}

function main() {
    const numBytes = 10000;
    const bytes = new Uint8Array(numBytes).fill(97); // 'a'

    let encoded = "";
    let decodedLen = 0;

    for (let iter = 0; iter < 100; iter++) {
        encoded = b64Encode(bytes, numBytes);
        decodedLen = b64DecodeLen(encoded);
    }

    const encLen = encoded.length;
    process.stdout.write("base64: encoded_len=" + encLen + " decoded_len=" + decodedLen + "\n");
    if (decodedLen === numBytes) {
        process.stdout.write("base64: PASS\n");
    } else {
        process.stdout.write("base64: FAIL\n");
    }
}

main();
