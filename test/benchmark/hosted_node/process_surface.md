# Hosted Node process surface inventory

Source audited: `lambda/js/js_globals.cpp`, `js_get_process_object_value`.
This record is the N3.3 boundary: a host-owned process object is created before
Jube module attachment; only Node-observable additions may be installed by
`node-core` during `runtime_attach`.

| Surface | Owner | Rationale / status |
|---|---|---|
| `argv`, `argv0`, `execArgv`, `execPath`, `pid`, `ppid`, `platform`, `arch`, `version`, `versions`, `title`, `env`, `stdin`, `stdout`, `stderr`, `exitCode` | host core | Startup arguments, OS handles, and process identity are required before optional Node modules activate. |
| `exit`, `nextTick` | host core | The engine invokes these through launch, teardown, and queue-drain paths; `nextTick` binding remains host-owned. |
| `on`, `addListener`, `once`, `emit`, `off`, `removeListener`, `removeAllListeners`, `listenerCount`, `listeners`, `prependListener`, `prependOnceListener`, `emitWarning` | host core | Process event delivery is shared with host lifecycle and warning paths. |
| IPC `send`, `disconnect`, `connected` | host core | Pipe ownership and close ordering are part of the host process lifecycle. |
| `permission`, `config`, `features`, `release`, `allowedNodeEnvironmentFlags`, `report`, `binding`, `dlopen` | host core | The values are startup configuration or direct runtime-loader compatibility surfaces. |
| POSIX identity: `getuid`, `getgid`, `geteuid`, `getegid`, `getgroups` | node-core | Moved to `node_process.cpp`; these are read-only platform queries installed during `runtime_attach`. |
| `memoryUsage` | node-core | Moved to `lambda/module/node_core/node_process.cpp`; it is a self-contained platform query installed at `runtime_attach`. |
| `cpuUsage` | node-core | Moved to `lambda/module/node_core/node_process.cpp`; it is a self-contained platform query installed at `runtime_attach`. |
| `cwd`, `chdir`, `uptime` | node-core | Moved through the same platform/file abstraction already used by node-core path; chdir retains the legacy no-op return and failure logging. |
| `hrtime`, `hrtime.bigint` | node-core | Moved with the opaque decimal-to-BigInt script service; tuple construction and monotonic-clock ownership remain in `node_process.cpp`. |
| `abort` | node-core | Moved as the existing direct C-runtime termination wrapper; it has no host runtime state to preserve. |
| `kill` | node-core | Moved with an opaque host system-error adapter so POSIX failure still exposes Node's `code`, `errno`, and `syscall` fields. |
| `getActiveResourcesInfo` | node-core facade | Published by node-core through a session-scoped host inventory service; node-net will replace its current host list with rid enumeration. |
| `hasUncaughtExceptionCaptureCallback`, `setUncaughtExceptionCaptureCallback` | node-core | Moved with a session-owned persistent callback root; the current runtime exposes state only, so this does not alter core exception dispatch. |
| POSIX credentials: `setuid`, `setgid`, `seteuid`, `setegid`, `initgroups`, `setgroups` | node-core | Moved with the host coded-error adapter; the two legacy no-op compatibility methods remain no-op. |
| `umask` | node-core | Moved with an exact-integer Jube value service so floats preserve the legacy no-op behavior and coded type/range errors remain host-created. |
| `setSourceMapsEnabled` | node-core | Moved as the existing observable no-op compatibility method. |
| `constrainedMemory`, `availableMemory` | node-core | Moved to `lambda/module/node_core/node_process.cpp`; they retain the host's existing platform-read/fallback semantics. |
| `_getActiveHandles` | transitional node-core facade | Published through the session host inventory service; node-net must replace the current host list with rid-table enumeration. |

Harness references were audited in `lambda/js/test_shim/` and `test/node/`.
They require host-core `process.arch`, `config`, `env`, `execPath`, `features`,
`nextTick`, `on`, `pid`, `platform`, `umask`, and `versions`; the focused
`test/node/process_extended.js` contract covers the two node-core extensions.
