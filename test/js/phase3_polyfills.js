// Phase 3: Promise.withResolvers + Minor Polyfills tests

// === 1. Promise.withResolvers ===
const pwr = Promise.withResolvers();
const pwr_has_promise = typeof pwr.promise === "object";
const pwr_has_resolve = typeof pwr.resolve === "function";
const pwr_has_reject = typeof pwr.reject === "function";

// Resolve the promise and check it works
pwr.resolve(42);

// === 2. Missing Math functions ===
const math_sinh = Math.sinh(1);
const math_cosh = Math.cosh(0);
const math_tanh = Math.tanh(0);
const math_asinh = Math.asinh(0);
const math_acosh = Math.acosh(1);
const math_atanh = Math.atanh(0);
const math_expm1 = Math.expm1(0);
const math_log1p = Math.log1p(0);

// Verify values
const math_sinh_ok = Math.abs(math_sinh - 1.1752011936438014) < 0.0001;
const math_cosh_ok = math_cosh === 1;
const math_tanh_ok = math_tanh === 0;
const math_asinh_ok = math_asinh === 0;
const math_acosh_ok = math_acosh === 0;
const math_atanh_ok = math_atanh === 0;
const math_expm1_ok = math_expm1 === 0;
const math_log1p_ok = math_log1p === 0;

// === 3. String.match ===
const match_result = "hello world".match(/o/g);
const match_count = match_result ? match_result.length : 0;

const match_single = "abc123def".match(/[0-9]+/);
const match_single_val = match_single ? match_single[0] : null;

// === 4. String.matchAll ===
const matchall_input = "test1 test2 test3";
const matchall_iter = matchall_input.matchAll(/test(\d)/g);
const matchall_result = [...matchall_iter];
const matchall_count = matchall_result ? matchall_result.length : 0;
const matchall_first = matchall_count > 0 ? matchall_result[0][0] : null;
const matchall_first_group = matchall_count > 0 ? matchall_result[0][1] : null;

// === 5. TextEncoder ===
const encoder = new TextEncoder();
const encoder_encoding = encoder.encoding;
const encoded = encoder.encode("ABC");
const encoded_len = encoded.length;
const encoded_0 = encoded[0];
const encoded_1 = encoded[1];
const encoded_2 = encoded[2];

// === 6. TextDecoder ===
const decoder = new TextDecoder();
const decoder_encoding = decoder.encoding;
const decoded = decoder.decode(encoded);

// === 7. WeakMap ===
const wm = new WeakMap();
const wm_key = {};
wm.set(wm_key, "weak_value");
const wm_val = wm.get(wm_key);
const wm_has = wm.has(wm_key);

// === 8. WeakSet ===
const ws = new WeakSet();
const ws_key = {};
ws.add(ws_key);
const ws_has = ws.has(ws_key);

const result = {
  pwr_has_promise: pwr_has_promise,
  pwr_has_resolve: pwr_has_resolve,
  pwr_has_reject: pwr_has_reject,
  math_sinh_ok: math_sinh_ok,
  math_cosh_ok: math_cosh_ok,
  math_tanh_ok: math_tanh_ok,
  math_asinh_ok: math_asinh_ok,
  math_acosh_ok: math_acosh_ok,
  math_atanh_ok: math_atanh_ok,
  math_expm1_ok: math_expm1_ok,
  math_log1p_ok: math_log1p_ok,
  match_count: match_count,
  match_single_val: match_single_val,
  matchall_count: matchall_count,
  matchall_first: matchall_first,
  matchall_first_group: matchall_first_group,
  encoder_encoding: encoder_encoding,
  encoded_len: encoded_len,
  encoded_0: encoded_0,
  encoded_1: encoded_1,
  encoded_2: encoded_2,
  decoder_encoding: decoder_encoding,
  decoded: decoded,
  wm_val: wm_val,
  wm_has: wm_has,
  ws_has: ws_has
};

console.log(JSON.stringify(result, null, 2));
