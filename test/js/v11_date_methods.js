// Test 1: new Date().getTime() returns a number
var d = new Date();
var t = d.getTime();
console.log(t > 0);

// Test 2: getFullYear returns a 4-digit year
console.log(d.getFullYear() >= 2024);

// Test 3: getMonth returns 0-11
var month = d.getMonth();
console.log(month >= 0 && month <= 11);

// Test 4: getDate returns 1-31
var day = d.getDate();
console.log(day >= 1 && day <= 31);

// Test 5: getHours returns 0-23
var hours = d.getHours();
console.log(hours >= 0 && hours <= 23);

// Test 6: getMinutes returns 0-59
var mins = d.getMinutes();
console.log(mins >= 0 && mins <= 59);

// Test 7: getSeconds returns 0-59
var secs = d.getSeconds();
console.log(secs >= 0 && secs <= 59);

// Test 8: getMilliseconds returns 0-999
var ms = d.getMilliseconds();
console.log(ms >= 0 && ms <= 999);

// Test 9: toISOString returns a string with T and Z
var iso = d.toISOString();
console.log(iso.indexOf("T") > 0);
console.log(iso.indexOf("Z") > 0);

// Test 10: Date.now() returns a number
console.log(Date.now() > 0);
