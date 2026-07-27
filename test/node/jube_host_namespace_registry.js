const names = [
  'fs', 'fs/promises', 'internal/fs/promises', 'internal/fs/utils',
  'child_process', 'crypto', 'dns', 'dns/promises', 'net', 'internal/net',
  'internal/js_stream_socket', 'tls', 'http', '_http_agent', '_http_common',
  '_http_server', '_http_outgoing', 'https', 'internal/errors',
  'internal/assert/myers_diff', 'internal/async_hooks',
  'internal/async_context_frame', 'internal/streams/add-abort-signal',
  'internal/streams/end-of-stream', 'internal/streams/state',
  'internal/crypto/util', 'internal/util', 'internal/util/inspect',
  'internal/repl', 'internal/test/binding',
];

for (const name of names) {
  console.log(name, typeof require(name));
}
console.log(require('fs') === require('node:fs'));
console.log(require('http') === require('_http_agent'));
console.log(require('dns/promises') === require('node:dns/promises'));

const moduleApi = require('module');
console.log(moduleApi.isBuiltin('node:fs'), moduleApi.isBuiltin('internal/test/binding'),
  !moduleApi.isBuiltin('not-a-builtin'));
console.log(moduleApi.builtinModules.includes('fs'),
  moduleApi.builtinModules.includes('internal/util/inspect'),
  moduleApi.builtinModules.includes('child_process'));
