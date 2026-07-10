// os module basic tests
import os from 'os';

// os.platform()
console.log(typeof os.platform());

// os.arch()
console.log(typeof os.arch());

// os.type()
console.log(typeof os.type());

// os.hostname()
console.log(typeof os.hostname());

// os.homedir()
console.log(typeof os.homedir());

// os.tmpdir()
console.log(typeof os.tmpdir());

// os.totalmem()
console.log(typeof os.totalmem());

// os.freemem()
console.log(typeof os.freemem());

// os.cpus() returns an array
const cpus = os.cpus();
console.log(Array.isArray(cpus));
const firstCpu = cpus[0] || { times: {} };
console.log(typeof firstCpu.speed);
console.log(typeof firstCpu.times.user);

// os.uptime()
console.log(typeof os.uptime());

// os.endianness()
const endianness = os.endianness();
console.log(endianness === 'LE' || endianness === 'BE');

// os.EOL
console.log(typeof os.EOL);

// os.userInfo()
const info = os.userInfo();
console.log(typeof info);
console.log(typeof info.username);
