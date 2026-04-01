// Slow SHA-384/512 test — original pdf.js Word64-based implementation
// From pdf.js src/core/crypto.js (Word64 class emulates 64-bit integers in JS)
//
// This is the code that was replaced by native C in js_crypto.cpp.
// Use this to benchmark JS-only SHA performance:
//   time ./lambda.exe js test/js/slow/sha512_word64.js
//   time node test/js/slow/sha512_word64.js

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function shadow(obj, prop, value) {
  Object.defineProperty(obj, prop, {
    value,
    enumerable: true,
    configurable: true,
    writable: false
  });
  return value;
}

// --- Word64: pdf.js 64-bit integer emulation class ---

var Word64 = class {
  constructor(highInteger, lowInteger) {
    this.high = highInteger | 0;
    this.low = lowInteger | 0;
  }
  and(word) {
    this.high &= word.high;
    this.low &= word.low;
  }
  xor(word) {
    this.high ^= word.high;
    this.low ^= word.low;
  }
  shiftRight(places) {
    if (places >= 32) {
      this.low = this.high >>> places - 32 | 0;
      this.high = 0;
    } else {
      this.low = this.low >>> places | this.high << 32 - places;
      this.high = this.high >>> places | 0;
    }
  }
  rotateRight(places) {
    let low, high;
    if (places & 32) {
      high = this.low;
      low = this.high;
    } else {
      low = this.low;
      high = this.high;
    }
    places &= 31;
    this.low = low >>> places | high << 32 - places;
    this.high = high >>> places | low << 32 - places;
  }
  not() {
    this.high = ~this.high;
    this.low = ~this.low;
  }
  add(word) {
    const lowAdd = (this.low >>> 0) + (word.low >>> 0);
    let highAdd = (this.high >>> 0) + (word.high >>> 0);
    if (lowAdd > 4294967295) {
      highAdd += 1;
    }
    this.low = lowAdd | 0;
    this.high = highAdd | 0;
  }
  copyTo(bytes, offset) {
    bytes[offset] = this.high >>> 24 & 255;
    bytes[offset + 1] = this.high >> 16 & 255;
    bytes[offset + 2] = this.high >> 8 & 255;
    bytes[offset + 3] = this.high & 255;
    bytes[offset + 4] = this.low >>> 24 & 255;
    bytes[offset + 5] = this.low >> 16 & 255;
    bytes[offset + 6] = this.low >> 8 & 255;
    bytes[offset + 7] = this.low & 255;
  }
  assign(word) {
    this.high = word.high;
    this.low = word.low;
  }
};

// --- SHA-512 constants (80 round constants) ---

var PARAMS = {
  get k() {
    return shadow(this, "k", [
      new Word64(1116352408, 3609767458), new Word64(1899447441, 602891725),
      new Word64(3049323471, 3964484399), new Word64(3921009573, 2173295548),
      new Word64(961987163, 4081628472), new Word64(1508970993, 3053834265),
      new Word64(2453635748, 2937671579), new Word64(2870763221, 3664609560),
      new Word64(3624381080, 2734883394), new Word64(310598401, 1164996542),
      new Word64(607225278, 1323610764), new Word64(1426881987, 3590304994),
      new Word64(1925078388, 4068182383), new Word64(2162078206, 991336113),
      new Word64(2614888103, 633803317), new Word64(3248222580, 3479774868),
      new Word64(3835390401, 2666613458), new Word64(4022224774, 944711139),
      new Word64(264347078, 2341262773), new Word64(604807628, 2007800933),
      new Word64(770255983, 1495990901), new Word64(1249150122, 1856431235),
      new Word64(1555081692, 3175218132), new Word64(1996064986, 2198950837),
      new Word64(2554220882, 3999719339), new Word64(2821834349, 766784016),
      new Word64(2952996808, 2566594879), new Word64(3210313671, 3203337956),
      new Word64(3336571891, 1034457026), new Word64(3584528711, 2466948901),
      new Word64(113926993, 3758326383), new Word64(338241895, 168717936),
      new Word64(666307205, 1188179964), new Word64(773529912, 1546045734),
      new Word64(1294757372, 1522805485), new Word64(1396182291, 2643833823),
      new Word64(1695183700, 2343527390), new Word64(1986661051, 1014477480),
      new Word64(2177026350, 1206759142), new Word64(2456956037, 344077627),
      new Word64(2730485921, 1290863460), new Word64(2820302411, 3158454273),
      new Word64(3259730800, 3505952657), new Word64(3345764771, 106217008),
      new Word64(3516065817, 3606008344), new Word64(3600352804, 1432725776),
      new Word64(4094571909, 1467031594), new Word64(275423344, 851169720),
      new Word64(430227734, 3100823752), new Word64(506948616, 1363258195),
      new Word64(659060556, 3750685593), new Word64(883997877, 3785050280),
      new Word64(958139571, 3318307427), new Word64(1322822218, 3812723403),
      new Word64(1537002063, 2003034995), new Word64(1747873779, 3602036899),
      new Word64(1955562222, 1575990012), new Word64(2024104815, 1125592928),
      new Word64(2227730452, 2716904306), new Word64(2361852424, 442776044),
      new Word64(2428436474, 593698344), new Word64(2756734187, 3733110249),
      new Word64(3204031479, 2999351573), new Word64(3329325298, 3815920427),
      new Word64(3391569614, 3928383900), new Word64(3515267271, 566280711),
      new Word64(3940187606, 3454069534), new Word64(4118630271, 4000239992),
      new Word64(116418474, 1914138554), new Word64(174292421, 2731055270),
      new Word64(289380356, 3203993006), new Word64(460393269, 320620315),
      new Word64(685471733, 587496836), new Word64(852142971, 1086792851),
      new Word64(1017036298, 365543100), new Word64(1126000580, 2618297676),
      new Word64(1288033470, 3409855158), new Word64(1501505948, 4234509866),
      new Word64(1607167915, 987167468), new Word64(1816402316, 1246189591)
    ]);
  }
};

// --- SHA-512 helper functions ---

function ch(result, x, y, z, tmp) {
  result.assign(x);
  result.and(y);
  tmp.assign(x);
  tmp.not();
  tmp.and(z);
  result.xor(tmp);
}

function maj(result, x, y, z, tmp) {
  result.assign(x);
  result.and(y);
  tmp.assign(x);
  tmp.and(z);
  result.xor(tmp);
  tmp.assign(y);
  tmp.and(z);
  result.xor(tmp);
}

function sigma(result, x, tmp) {
  result.assign(x);
  result.rotateRight(28);
  tmp.assign(x);
  tmp.rotateRight(34);
  result.xor(tmp);
  tmp.assign(x);
  tmp.rotateRight(39);
  result.xor(tmp);
}

function sigmaPrime(result, x, tmp) {
  result.assign(x);
  result.rotateRight(14);
  tmp.assign(x);
  tmp.rotateRight(18);
  result.xor(tmp);
  tmp.assign(x);
  tmp.rotateRight(41);
  result.xor(tmp);
}

function littleSigma(result, x, tmp) {
  result.assign(x);
  result.rotateRight(1);
  tmp.assign(x);
  tmp.rotateRight(8);
  result.xor(tmp);
  tmp.assign(x);
  tmp.shiftRight(7);
  result.xor(tmp);
}

function littleSigmaPrime(result, x, tmp) {
  result.assign(x);
  result.rotateRight(19);
  tmp.assign(x);
  tmp.rotateRight(61);
  result.xor(tmp);
  tmp.assign(x);
  tmp.shiftRight(6);
  result.xor(tmp);
}

// --- calculateSHA512 (original pdf.js implementation) ---

function calcSHA512_js(data, offset, length, mode384) {
  if (mode384 === undefined) mode384 = false;
  let h0, h1, h2, h3, h4, h5, h6, h7;
  if (!mode384) {
    h0 = new Word64(1779033703, 4089235720);
    h1 = new Word64(3144134277, 2227873595);
    h2 = new Word64(1013904242, 4271175723);
    h3 = new Word64(2773480762, 1595750129);
    h4 = new Word64(1359893119, 2917565137);
    h5 = new Word64(2600822924, 725511199);
    h6 = new Word64(528734635, 4215389547);
    h7 = new Word64(1541459225, 327033209);
  } else {
    h0 = new Word64(3418070365, 3238371032);
    h1 = new Word64(1654270250, 914150663);
    h2 = new Word64(2438529370, 812702999);
    h3 = new Word64(355462360, 4144912697);
    h4 = new Word64(1731405415, 4290775857);
    h5 = new Word64(2394180231, 1750603025);
    h6 = new Word64(3675008525, 1694076839);
    h7 = new Word64(1203062813, 3204075428);
  }
  const paddedLength = Math.ceil((length + 17) / 128) * 128;
  const padded = new Uint8Array(paddedLength);
  let i, j;
  for (i = 0; i < length; ++i) {
    padded[i] = data[offset++];
  }
  padded[i++] = 128;
  const n = paddedLength - 16;
  if (i < n) {
    i = n;
  }
  i += 11;
  padded[i++] = length >>> 29 & 255;
  padded[i++] = length >> 21 & 255;
  padded[i++] = length >> 13 & 255;
  padded[i++] = length >> 5 & 255;
  padded[i++] = length << 3 & 255;
  const w = new Array(80);
  for (i = 0; i < 80; i++) {
    w[i] = new Word64(0, 0);
  }
  const { k } = PARAMS;
  let a = new Word64(0, 0), b = new Word64(0, 0), c = new Word64(0, 0);
  let d = new Word64(0, 0), e = new Word64(0, 0), f = new Word64(0, 0);
  let g = new Word64(0, 0), h = new Word64(0, 0);
  const t1 = new Word64(0, 0), t2 = new Word64(0, 0);
  const tmp1 = new Word64(0, 0), tmp2 = new Word64(0, 0);
  let tmp3;
  for (i = 0; i < paddedLength; ) {
    for (j = 0; j < 16; ++j) {
      w[j].high = padded[i] << 24 | padded[i + 1] << 16 | padded[i + 2] << 8 | padded[i + 3];
      w[j].low = padded[i + 4] << 24 | padded[i + 5] << 16 | padded[i + 6] << 8 | padded[i + 7];
      i += 8;
    }
    for (j = 16; j < 80; ++j) {
      tmp3 = w[j];
      littleSigmaPrime(tmp3, w[j - 2], tmp2);
      tmp3.add(w[j - 7]);
      littleSigma(tmp1, w[j - 15], tmp2);
      tmp3.add(tmp1);
      tmp3.add(w[j - 16]);
    }
    a.assign(h0);
    b.assign(h1);
    c.assign(h2);
    d.assign(h3);
    e.assign(h4);
    f.assign(h5);
    g.assign(h6);
    h.assign(h7);
    for (j = 0; j < 80; ++j) {
      t1.assign(h);
      sigmaPrime(tmp1, e, tmp2);
      t1.add(tmp1);
      ch(tmp1, e, f, g, tmp2);
      t1.add(tmp1);
      t1.add(k[j]);
      t1.add(w[j]);
      sigma(t2, a, tmp2);
      maj(tmp1, a, b, c, tmp2);
      t2.add(tmp1);
      tmp3 = h;
      h = g;
      g = f;
      f = e;
      d.add(t1);
      e = d;
      d = c;
      c = b;
      b = a;
      tmp3.assign(t1);
      tmp3.add(t2);
      a = tmp3;
    }
    h0.add(a);
    h1.add(b);
    h2.add(c);
    h3.add(d);
    h4.add(e);
    h5.add(f);
    h6.add(g);
    h7.add(h);
  }
  let result;
  if (!mode384) {
    result = new Uint8Array(64);
    h0.copyTo(result, 0);
    h1.copyTo(result, 8);
    h2.copyTo(result, 16);
    h3.copyTo(result, 24);
    h4.copyTo(result, 32);
    h5.copyTo(result, 40);
    h6.copyTo(result, 48);
    h7.copyTo(result, 56);
  } else {
    result = new Uint8Array(48);
    h0.copyTo(result, 0);
    h1.copyTo(result, 8);
    h2.copyTo(result, 16);
    h3.copyTo(result, 24);
    h4.copyTo(result, 32);
    h5.copyTo(result, 40);
  }
  return result;
}

function calcSHA384_js(data, offset, length) {
  return calcSHA512_js(data, offset, length, true);
}

// ============================================================================
// Test: compute SHA-512 and SHA-384 on a 2304-byte buffer (same size as
// the AES-encrypted output in PDF20._hash), and verify known results.
// This matches what the PDF20Algorithm tests exercise in the inner loop.
// ============================================================================

function toHex(arr) {
  var s = "";
  for (var i = 0; i < arr.length; i++) {
    var h = arr[i].toString(16);
    if (h.length < 2) h = "0" + h;
    s += h;
  }
  return s;
}

// Build a test buffer (2304 bytes = 36 * 64, same as crypto_spec k1 size)
var testData = new Uint8Array(2304);
for (var i = 0; i < 2304; i++) {
  testData[i] = i & 0xFF;
}

// Compute hashes
var t0 = Date.now();
var sha512Result = calcSHA512_js(testData, 0, testData.length);
var sha384Result = calcSHA384_js(testData, 0, testData.length);
var elapsed = Date.now() - t0;

// Known-good hashes (verified with Node.js crypto module)
var expected512 = "a05ac71deae25485e10a89fd83b015088243ea0d09c41302f3eed981da05c1aafdb557f27bfcecba0a64bae3685668788dff6acf36231105a7a0d70553280e3c";
var expected384 = "7ddbe21738ef24c7d61857996f45ecb0d08cd1da6c9360717faf446644b2c95da2316f939f342934ebbfe77a093609c3";

var hex512 = toHex(sha512Result);
var hex384 = toHex(sha384Result);

var pass512 = hex512 === expected512;
var pass384 = hex384 === expected384;

console.log("SHA-512: " + (pass512 ? "PASS" : "FAIL") + " (" + elapsed + "ms)");
console.log("SHA-384: " + (pass384 ? "PASS" : "FAIL"));

if (!pass512) {
  console.log("  expected: " + expected512);
  console.log("  got:      " + hex512);
}
if (!pass384) {
  console.log("  expected: " + expected384);
  console.log("  got:      " + hex384);
}

// Now do a heavier benchmark: 50 iterations (similar to PDF20._hash loop)
var t1 = Date.now();
for (var iter = 0; iter < 50; iter++) {
  calcSHA512_js(testData, 0, testData.length);
  calcSHA384_js(testData, 0, testData.length);
}
var benchMs = Date.now() - t1;
console.log("Benchmark: 50x (SHA-512 + SHA-384) on 2304 bytes = " + benchMs + "ms");
