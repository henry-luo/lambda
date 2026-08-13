var unhandled_count = 0;
process.on("unhandledRejection", function() { unhandled_count++; });
var i = 0;
while (i < 1100) { Promise.reject(i); i++; }

setTimeout(function() {
    console.log(unhandled_count);
}, 0);
