// Spread element tests

// Basic spread in array
var a1 = [1, 2, 3];
var a2 = [0, ...a1, 4];
var s1_len = a2.length;
var s1_0 = a2[0];
var s1_1 = a2[1];
var s1_2 = a2[2];
var s1_3 = a2[3];
var s1_4 = a2[4];

// Spread at beginning
var a3 = [...a1, 10];
var s2_len = a3.length;
var s2_0 = a3[0];
var s2_3 = a3[3];

// Spread at end
var a4 = [10, ...a1];
var s3_len = a4.length;
var s3_0 = a4[0];
var s3_1 = a4[1];

// Multiple spreads
var b1 = [1, 2];
var b2 = [3, 4];
var a5 = [...b1, ...b2];
var s4_len = a5.length;
var s4_0 = a5[0];
var s4_1 = a5[1];
var s4_2 = a5[2];
var s4_3 = a5[3];

// Spread empty array
var a6 = [1, ...[], 2];
var s5_len = a6.length;
var s5_0 = a6[0];
var s5_1 = a6[1];

const result = {
  s1_len: s1_len,
  s1_0: s1_0,
  s1_1: s1_1,
  s1_2: s1_2,
  s1_3: s1_3,
  s1_4: s1_4,
  s2_len: s2_len,
  s2_0: s2_0,
  s2_3: s2_3,
  s3_len: s3_len,
  s3_0: s3_0,
  s3_1: s3_1,
  s4_len: s4_len,
  s4_0: s4_0,
  s4_1: s4_1,
  s4_2: s4_2,
  s4_3: s4_3,
  s5_len: s5_len,
  s5_0: s5_0,
  s5_1: s5_1
};
result;
