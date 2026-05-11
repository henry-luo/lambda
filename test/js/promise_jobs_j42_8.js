// J42-8: Promise jobs share the Promise Resolution Procedure.

var log = [];

var thenable = {
    then: function(resolve, reject) {
        log.push("thenable job");
        resolve("assimilated");
    }
};

Promise.resolve(thenable).then(function(value) {
    log.push("resolve:" + value);
});

Promise.resolve("x")
    .then(function(value) {
        throw "boom";
    })
    .catch(function(reason) {
        log.push("caught:" + reason);
    });

new Promise(function(resolve, reject) {
    throw "executor";
}).catch(function(reason) {
    log.push("executor:" + reason);
});

Promise.all([1, thenable, Promise.resolve(3)]).then(function(values) {
    log.push("all:" + values[0] + "," + values[1] + "," + values[2]);
});

setTimeout(function() {
    for (var i = 0; i < log.length; i++) {
        console.log(log[i]);
    }
}, 0);

0;
