// process extended tests — memoryUsage, versions

// process.versions
console.log('versions type:', typeof process.versions);
console.log('versions.node:', typeof process.versions.node);

// process.memoryUsage
var mem = process.memoryUsage();
console.log('memoryUsage type:', typeof mem);
console.log('rss > 0:', mem.rss > 0);

// process.cpuUsage
var cpu = process.cpuUsage();
console.log('cpuUsage type:', typeof cpu);

// process.umask
var mask = process.umask();
console.log('umask type:', typeof mask);

// process.title
console.log('title type:', typeof process.title);

// process.pid > 0
console.log('pid > 0:', process.pid > 0);

// process.ppid
console.log('ppid type:', typeof process.ppid);
