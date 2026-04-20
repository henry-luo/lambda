var moment = require('./moment.min.js');
var REF = "2024-06-15T14:30:45.123Z";

var m = moment.utc(REF).startOf('hour');
console.log("minute:", m.minute());
console.log("second:", m.second());
console.log("hour:", m.hour());
console.log("format:", m.format());
console.log("result:", m.minute() === 0 && m.second() === 0);
