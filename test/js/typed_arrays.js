// Typed array tests

// Uint8Array
var u8 = new Uint8Array(3);
u8[0] = 65;
u8[1] = 66;
u8[2] = 67;
var u8_0 = u8[0];
var u8_1 = u8[1];
var u8_2 = u8[2];
var u8_len = u8.length;

// Int32Array
var i32 = new Int32Array(2);
i32[0] = -100;
i32[1] = 200;
var i32_0 = i32[0];
var i32_1 = i32[1];
var i32_len = i32.length;

// Float64Array
var f64 = new Float64Array(2);
f64[0] = 3.14;
f64[1] = 2.718;
var f64_0 = f64[0];
var f64_1 = f64[1];

const result = {
  u8_0: u8_0,
  u8_1: u8_1,
  u8_2: u8_2,
  u8_len: u8_len,
  i32_0: i32_0,
  i32_1: i32_1,
  i32_len: i32_len,
  f64_0: f64_0,
  f64_1: f64_1
};
result;
