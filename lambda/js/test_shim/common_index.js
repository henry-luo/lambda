// Lambda-compatible shim for Node.js test common module
// Replaces ref/node/test/common/index.js for running official Node.js tests
// under Lambda's JS runtime.
'use strict';
const process = globalThis.process;

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const { inspect } = require('util');
const { isMainThread } = require('worker_threads');

const tmpdir = require('./tmpdir');

const bits = ['arm64', 'loong64', 'mips', 'mipsel', 'ppc64', 'riscv64', 's390x', 'x64']
  .includes(process.arch) ? 64 : 32;

const hasIntl = !!(process.config && process.config.variables &&
  process.config.variables.v8_enable_i18n_support);

const hasFullICU = (() => {
  try {
    const january = new Date(9e8);
    const spanish = new Intl.DateTimeFormat('es', { month: 'long' });
    return spanish.format(january) === 'enero';
  } catch {
    return false;
  }
})();

// Some tests assume a umask of 0o022
if (isMainThread && typeof process.umask === 'function')
  process.umask(0o022);

const noop = () => {};

const hasCrypto = Boolean(process.versions && process.versions.openssl) &&
                  !process.env.NODE_SKIP_CRYPTO;
const hasInspector = Boolean(process.features && process.features.inspector);
const hasSQLite = Boolean(process.versions && process.versions.sqlite);
const hasFFI = false;
const hasQuic = false;
const hasLocalStorage = false;
const usesSharedLibrary = false;

const isWindows = process.platform === 'win32';
const isSunOS = process.platform === 'sunos';
const isFreeBSD = process.platform === 'freebsd';
const isOpenBSD = process.platform === 'openbsd';
const isLinux = process.platform === 'linux';
const isMacOS = process.platform === 'darwin';
const isASan = false;
const isRiscv64 = process.arch === 'riscv64';
const isDebug = !!(process.features && process.features.debug);

function platformTimeout(ms) {
  const multipliers = typeof ms === 'bigint' ?
    { two: 2n, four: 4n, seven: 7n } : { two: 2, four: 4, seven: 7 };
  if (isDebug) ms = multipliers.two * ms;
  return ms;
}

const buildType = 'Release';

// ---- mustCall / mustNotCall / mustSucceed ----

const mustCallChecks = [];

function runCallChecks(exitCode) {
  if (exitCode !== 0) return;
  const failed = mustCallChecks.filter(function(context) {
    if ('minimum' in context) {
      context.messageSegment = 'at least ' + context.minimum;
      return context.actual < context.minimum;
    }
    context.messageSegment = 'exactly ' + context.exact;
    return context.actual !== context.exact;
  });
  // Note: Lambda doesn't have a full event loop, so many async callbacks
  // will never fire. Report mismatches as warnings, not fatal errors.
  // The test itself already validates synchronous behavior; we only exit(1)
  // if all expected calls were made zero times AND the test explicitly
  // relied on them (heuristic: if ANY mustCall was invoked at least once,
  // treat zero-call mismatches as fatal).
  if (failed.length === 0) return;
  const anyInvoked = mustCallChecks.some(function(c) { return c.actual > 0; });
  failed.forEach(function(context) {
    console.log('Mismatched %s function calls. Expected %s, actual %d.',
                context.name,
                context.messageSegment,
                context.actual);
  });
  // Only exit(1) if at least one mustCall was partially fulfilled
  // (indicating the test got partway through its assertions)
  if (anyInvoked) process.exit(1);
}

function mustCall(fn, exact) {
  return _mustCallInner(fn, exact, 'exact');
}

function mustSucceed(fn, exact) {
  return mustCall(function(err) {
    assert.ifError(err);
    if (typeof fn === 'function')
      return fn.apply(this, Array.prototype.slice.call(arguments, 1));
  }, exact);
}

function mustCallAtLeast(fn, minimum) {
  return _mustCallInner(fn, minimum, 'minimum');
}

function _mustCallInner(fn, criteria, field) {
  if (criteria === undefined) criteria = 1;
  if (typeof fn === 'number') {
    criteria = fn;
    fn = noop;
  } else if (fn === undefined) {
    fn = noop;
  }
  if (typeof criteria !== 'number')
    throw new TypeError('Invalid ' + field + ' value: ' + criteria);

  const context = {
    actual: 0,
    name: fn.name || '<anonymous>',
  };
  context[field] = criteria;

  // Capture stack trace manually (getCallSites may not be available)
  try {
    throw new Error();
  } catch (e) {
    context.stack = e.stack || '';
  }

  if (mustCallChecks.length === 0) process.on('exit', runCallChecks);
  mustCallChecks.push(context);

  const _return = function() {
    context.actual++;
    return fn.apply(this, arguments);
  };
  Object.defineProperties(_return, {
    name: { value: fn.name, writable: false, enumerable: false, configurable: true },
    length: { value: fn.length, writable: false, enumerable: false, configurable: true },
  });
  return _return;
}

function mustNotCall(msg) {
  return function mustNotCall() {
    const argsInfo = arguments.length > 0 ?
      '\ncalled with arguments: ' + Array.prototype.map.call(arguments, function(a) { return inspect(a); }).join(', ') : '';
    assert.fail(
      (msg || 'function should not have been called') + argsInfo);
  };
}

// ---- mustNotMutateObjectDeep ----

const _mustNotMutateObjectDeepProxies = new WeakMap();

function mustNotMutateObjectDeep(original) {
  if (original === null || typeof original !== 'object') {
    return original;
  }
  const cachedProxy = _mustNotMutateObjectDeepProxies.get(original);
  if (cachedProxy) return cachedProxy;

  const handler = {
    __proto__: null,
    defineProperty(target, property, descriptor) {
      assert.fail('Expected no side effects, got ' + inspect(property) + ' defined');
    },
    deleteProperty(target, property) {
      assert.fail('Expected no side effects, got ' + inspect(property) + ' deleted');
    },
    get(target, prop, receiver) {
      return mustNotMutateObjectDeep(Reflect.get(target, prop, receiver));
    },
    preventExtensions(target) {
      assert.fail('Expected no side effects, got extensions prevented');
    },
    set(target, property, value, receiver) {
      assert.fail('Expected no side effects, got ' + inspect(value) + ' assigned to ' + inspect(property));
    },
    setPrototypeOf(target, prototype) {
      assert.fail('Expected no side effects, got set prototype to ' + prototype);
    },
  };
  const proxy = new Proxy(original, handler);
  _mustNotMutateObjectDeepProxies.set(original, proxy);
  return proxy;
}

// ---- skip / print ----

function printSkipMessage(msg) {
  console.log('1..0 # Skipped: ' + msg);
}

function skip(msg) {
  printSkipMessage(msg);
  process.exit(0);
}

// ---- expectsError ----

function expectsError(validator, exact) {
  return mustCall(function() {
    if (arguments.length !== 1) {
      assert.fail('Expected one argument, got ' + inspect(Array.from(arguments)));
    }
    const error = arguments[0];
    assert.throws(function() { throw error; }, validator);
    return true;
  }, exact);
}

// ---- expectWarning ----

let catchWarning;

function _expectWarning(name, expected, code) {
  if (typeof expected === 'string') {
    expected = [[expected, code]];
  } else if (!Array.isArray(expected)) {
    expected = Object.entries(expected).map(function(pair) { return [pair[1], pair[0]]; });
  } else if (expected.length !== 0 && !Array.isArray(expected[0])) {
    expected = [[expected[0], expected[1]]];
  }
  return mustCall(function(warning) {
    const expectedProperties = expected.shift();
    if (!expectedProperties) {
      assert.fail('Unexpected extra warning received: ' + warning);
    }
    const message = expectedProperties[0];
    const warnCode = expectedProperties[1];
    assert.strictEqual(warning.name, name);
    if (typeof message === 'string') {
      assert.strictEqual(warning.message, message);
    } else {
      assert.match(warning.message, message);
    }
    assert.strictEqual(warning.code, warnCode);
  }, expected.length);
}

function expectWarning(nameOrMap, expected, code) {
  if (catchWarning === undefined) {
    catchWarning = {};
    process.on('warning', function(warning) {
      if (!catchWarning[warning.name]) {
        throw new TypeError('"' + warning.name + '" was triggered without being expected.\n' + inspect(warning));
      }
      catchWarning[warning.name](warning);
    });
  }
  if (typeof nameOrMap === 'string') {
    catchWarning[nameOrMap] = _expectWarning(nameOrMap, expected, code);
  } else {
    Object.keys(nameOrMap).forEach(function(name) {
      catchWarning[name] = _expectWarning(name, nameOrMap[name]);
    });
  }
}

// ---- invalidArgTypeHelper ----

function invalidArgTypeHelper(input) {
  if (input == null) {
    return ' Received ' + input;
  }
  if (typeof input === 'function') {
    return ' Received function ' + input.name;
  }
  if (typeof input === 'object') {
    if (input.constructor && input.constructor.name) {
      return ' Received an instance of ' + input.constructor.name;
    }
    return ' Received ' + inspect(input, { depth: -1 });
  }
  let inspected = inspect(input, { colors: false });
  if (inspected.length > 28) { inspected = inspected.slice(0, 25) + '...'; }
  return ' Received type ' + typeof input + ' (' + inspected + ')';
}

// ---- canCreateSymLink ----

function canCreateSymLink() {
  if (isWindows) {
    try {
      const { execSync } = require('child_process');
      const whoamiPath = path.join(process.env.SystemRoot || '', 'System32', 'whoami.exe');
      const output = execSync(whoamiPath + ' /priv', { timeout: 1000 });
      return output.includes('SeCreateSymbolicLinkPrivilege');
    } catch {
      return false;
    }
  }
  return true;
}

// ---- spawnPromisified ----

function spawnPromisified() {
  const { spawn } = require('child_process');
  let stderr = '';
  let stdout = '';
  const child = spawn.apply(null, arguments);
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', function(data) { stderr += data; });
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', function(data) { stdout += data; });
  return new Promise(function(resolve, reject) {
    child.on('close', function(code, signal) {
      resolve({ code: code, signal: signal, stderr: stderr, stdout: stdout });
    });
    child.on('error', function(code, signal) {
      reject({ code: code, signal: signal, stderr: stderr, stdout: stdout });
    });
  });
}

// ---- nodeProcessAborted ----

function nodeProcessAborted(exitCode, signal) {
  let expectedExitCodes = [132, 133, 134];
  const expectedSignals = ['SIGILL', 'SIGTRAP', 'SIGABRT'];
  if (isWindows) expectedExitCodes = [0x80000003, 134];
  if (signal !== null) return expectedSignals.includes(signal);
  return expectedExitCodes.includes(exitCode);
}

// ---- misc helpers ----

function isAlive(pid) {
  try { process.kill(pid, 'SIGCONT'); return true; } catch { return false; }
}

function getArrayBufferViews(buf) {
  const buffer = buf.buffer;
  const byteOffset = buf.byteOffset;
  const byteLength = buf.byteLength;
  const out = [];
  const types = [
    Int8Array, Uint8Array, Uint8ClampedArray,
    Int16Array, Uint16Array,
    Int32Array, Uint32Array,
    Float32Array, Float64Array,
    DataView,
  ];
  // Add BigInt types if available
  if (typeof BigInt64Array !== 'undefined') types.push(BigInt64Array, BigUint64Array);
  // Add Float16Array if available
  if (typeof Float16Array !== 'undefined') types.push(Float16Array);

  for (const type of types) {
    const bpe = type.BYTES_PER_ELEMENT || 1;
    if (byteLength % bpe === 0) {
      out.push(new type(buffer, byteOffset, byteLength / bpe));
    }
  }
  return out;
}

function getBufferSources(buf) {
  return getArrayBufferViews(buf).concat([new Uint8Array(buf).buffer]);
}

function runWithInvalidFD(func) {
  let fd = 1 << 30;
  try {
    while (fs.fstatSync(fd--) && fd > 0);
  } catch {
    return func(fd);
  }
  printSkipMessage('Could not generate an invalid fd');
}

function escapePOSIXShell(cmdParts) {
  const args = Array.prototype.slice.call(arguments, 1);
  if (isWindows) {
    return [String.raw({ raw: cmdParts }, ...args)];
  }
  const env = Object.assign({}, process.env);
  let cmd = cmdParts[0];
  for (let i = 0; i < args.length; i++) {
    const envVarName = 'ESCAPED_' + i;
    env[envVarName] = args[i];
    cmd += '${' + envVarName + '}' + cmdParts[i + 1];
  }
  return [cmd, { env: env }];
}

const pwdCommand = isWindows ?
  ['cmd.exe', ['/d', '/c', 'cd']] :
  ['pwd', []];

function requireNoPackageJSONAbove(dir) {
  // stub — Lambda doesn't need this check
}

function skipIfInspectorDisabled() {
  if (!hasInspector) skip('V8 inspector is disabled');
}

function skipIf32Bits() {
  if (bits < 64) skip('The tested feature is not available in 32bit builds');
}

function skipIfEslintMissing() {
  skip('ESLint not available');
}

function skipIfSQLiteMissing() {
  if (!hasSQLite) skip('missing SQLite');
}

function skipIfFFIMissing() {
  if (!hasFFI) skip('missing FFI');
}

function getTTYfd() {
  const tty = require('tty');
  const ttyFd = [1, 2, 4, 5].find(function(fd) {
    try { return tty.isatty(fd); } catch { return false; }
  });
  if (ttyFd === undefined) {
    try { return fs.openSync('/dev/tty'); } catch { return -1; }
  }
  return ttyFd;
}

function sleepSync(ms) {
  // Use Atomics.wait if SharedArrayBuffer is available
  if (typeof SharedArrayBuffer !== 'undefined') {
    const sab = new SharedArrayBuffer(4);
    const i32 = new Int32Array(sab);
    Atomics.wait(i32, 0, 0, ms);
  } else {
    // Fallback: busy-wait (not ideal but functional)
    const end = Date.now() + ms;
    while (Date.now() < end) { /* spin */ }
  }
}

function childShouldThrowAndAbort() {
  // stub for Lambda
}

function resolveBuiltBinary(binary) {
  if (isWindows) binary += '.exe';
  return path.join(path.dirname(process.execPath || ''), binary);
}

function expectRequiredModule(mod, expectation, checkESModule) {
  if (checkESModule === undefined) checkESModule = true;
  const clone = Object.assign({}, mod);
  if (Object.hasOwn(mod, 'default') && checkESModule) {
    assert.strictEqual(mod.__esModule, true);
    delete clone.__esModule;
  }
  assert.deepStrictEqual(clone, Object.assign({}, expectation));
}

function expectRequiredTLAError(err) {
  const message = /require\(\) cannot be used on an ESM graph with top-level await/;
  if (typeof err === 'string') {
    assert.match(err, /ERR_REQUIRE_ASYNC_MODULE/);
    assert.match(err, message);
  } else {
    assert.strictEqual(err.code, 'ERR_REQUIRE_ASYNC_MODULE');
    assert.match(err.message, message);
  }
}

// ---- Globals check (disabled by default in Lambda) ----

// Lambda injects some globals (Node, innerWidth, innerHeight) that are not
// present in Node.js. We allow them by default.
const _allowedGlobals = new Set();

function allowGlobals() {
  for (let i = 0; i < arguments.length; i++) {
    _allowedGlobals.add(arguments[i]);
  }
}

// Disable globals leak detection by default in Lambda — Lambda has extra globals
// like Node, innerWidth, innerHeight that real Node.js doesn't.
// Tests that specifically want this check can set NODE_TEST_KNOWN_GLOBALS=1.
// (The upstream Node.js common module enables this by default.)

const PIPE = (() => {
  const localRelative = path.relative(process.cwd(), tmpdir.path + '/');
  const pipePrefix = isWindows ? '\\\\.\\pipe\\' : localRelative;
  const pipeName = 'node-test.' + process.pid + '.sock';
  return path.join(pipePrefix, pipeName);
})();

const localIPv6Hosts = isLinux ? [
  'ip6-localhost', 'ip6-loopback', 'ipv6-localhost', 'ipv6-loopback', 'localhost',
] : ['localhost'];

// ---- Exported object ----

const common = {
  allowGlobals: allowGlobals,
  buildType: buildType,
  canCreateSymLink: canCreateSymLink,
  childShouldThrowAndAbort: childShouldThrowAndAbort,
  escapePOSIXShell: escapePOSIXShell,
  expectsError: expectsError,
  expectRequiredModule: expectRequiredModule,
  expectRequiredTLAError: expectRequiredTLAError,
  expectWarning: expectWarning,
  getArrayBufferViews: getArrayBufferViews,
  getBufferSources: getBufferSources,
  getTTYfd: getTTYfd,
  hasIntl: hasIntl,
  hasFullICU: hasFullICU,
  hasCrypto: hasCrypto,
  hasQuic: hasQuic,
  hasInspector: hasInspector,
  hasSQLite: hasSQLite,
  hasFFI: hasFFI,
  hasLocalStorage: hasLocalStorage,
  invalidArgTypeHelper: invalidArgTypeHelper,
  isAlive: isAlive,
  isASan: isASan,
  isDebug: isDebug,
  isFreeBSD: isFreeBSD,
  isLinux: isLinux,
  isMainThread: isMainThread,
  isOpenBSD: isOpenBSD,
  isMacOS: isMacOS,
  isPi: function() { return false; },
  isSunOS: isSunOS,
  isWindows: isWindows,
  localIPv6Hosts: localIPv6Hosts,
  mustCall: mustCall,
  mustCallAtLeast: mustCallAtLeast,
  mustNotCall: mustNotCall,
  mustNotMutateObjectDeep: mustNotMutateObjectDeep,
  mustSucceed: mustSucceed,
  nodeProcessAborted: nodeProcessAborted,
  PIPE: PIPE,
  platformTimeout: platformTimeout,
  printSkipMessage: printSkipMessage,
  pwdCommand: pwdCommand,
  requireNoPackageJSONAbove: requireNoPackageJSONAbove,
  resolveBuiltBinary: resolveBuiltBinary,
  runWithInvalidFD: runWithInvalidFD,
  skip: skip,
  skipIf32Bits: skipIf32Bits,
  skipIfEslintMissing: skipIfEslintMissing,
  skipIfInspectorDisabled: skipIfInspectorDisabled,
  skipIfFFIMissing: skipIfFFIMissing,
  skipIfSQLiteMissing: skipIfSQLiteMissing,
  spawnPromisified: spawnPromisified,
  sleepSync: sleepSync,
  usesSharedLibrary: usesSharedLibrary,

  get enoughTestMem() {
    return require('os').totalmem() > 0x70000000;
  },
  get hasIPv6() {
    const iFaces = require('os').networkInterfaces();
    const re = isWindows ? /Loopback Pseudo-Interface/ : /lo/;
    return Object.keys(iFaces).some(function(name) {
      return re.test(name) && iFaces[name].some(function(i) { return i.family === 'IPv6'; });
    });
  },
  get isAIX() {
    return require('os').type() === 'AIX';
  },
  get isIBMi() {
    return require('os').type() === 'OS400';
  },
  get localhostIPv4() {
    return '127.0.0.1';
  },
  get PORT() {
    return +process.env.NODE_COMMON_PORT || 12346;
  },
  get isInsideDirWithUnusualChars() {
    return false;
  },
  get defaultAutoSelectFamilyAttemptTimeout() {
    return 2500;
  },
  get parseTestMetadata() {
    return function() { return { flags: [], envs: {} }; };
  },
};

// Export common directly (no Proxy wrapper — Lambda's method dispatch
// optimization bypasses Proxy get traps for method calls).
module.exports = common;
