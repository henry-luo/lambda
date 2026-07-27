const nodeProcess = require('node:process');
const bareProcess = require('process');

const memory = nodeProcess.memoryUsage();
const cpu = nodeProcess.cpuUsage();

console.log(nodeProcess === process);
console.log(bareProcess === process);
console.log(typeof memory, memory.rss > 0, typeof memory.heapUsed);
console.log(typeof cpu, typeof cpu.user, typeof cpu.system);
const currentDirectory = process.cwd();
console.log(typeof currentDirectory, process.chdir(currentDirectory) === undefined, process.cwd() === currentDirectory);
console.log(typeof process.uptime(), process.uptime() >= 0);
const timerStart = process.hrtime();
const timerDelta = process.hrtime(timerStart);
console.log(Array.isArray(timerStart), timerStart.length === 2, Array.isArray(timerDelta));
console.log(typeof process.hrtime.bigint());
console.log(typeof process.constrainedMemory(), typeof process.availableMemory());
const originalMask = process.umask();
const previousMask = process.umask('022');
console.log(typeof originalMask, previousMask === originalMask, process.umask() === 0o22);
process.umask(originalMask);
console.log(typeof process.setSourceMapsEnabled, process.setSourceMapsEnabled(true) === undefined);
console.log(typeof process.abort);
console.log(Array.isArray(process.getActiveResourcesInfo()));
console.log(Array.isArray(process._getActiveHandles()));
const captureCallback = () => {};
process.setUncaughtExceptionCaptureCallback(captureCallback);
console.log(process.hasUncaughtExceptionCaptureCallback());
process.setUncaughtExceptionCaptureCallback(null);
console.log(!process.hasUncaughtExceptionCaptureCallback());

if (process.platform !== 'win32') {
  console.log(process.kill(process.pid, '0'));
  console.log(typeof process.getuid, typeof process.getgid, typeof process.geteuid);
  console.log(typeof process.getegid, Array.isArray(process.getgroups()));
  console.log(typeof process.setuid, typeof process.setgid, typeof process.seteuid);
  console.log(typeof process.setegid, typeof process.initgroups, typeof process.setgroups);
}
