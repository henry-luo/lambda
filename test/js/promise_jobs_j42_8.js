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

var sourcePromise = Promise.resolve("source");
var adoptedPromise = new Promise(function(resolve, reject) {
    resolve(sourcePromise);
});
adoptedPromise.then(function(value) {
    log.push("adopted:" + value);
});
sourcePromise.then(function(value) {
    log.push("source:" + value);
});

var passThroughPromise = Promise.resolve("through").then();
passThroughPromise.then(function(value) {
    log.push("pass-through:" + value);
});
Promise.resolve("marker").then(function(value) {
    log.push("marker:" + value);
});

Promise.all([1, thenable, Promise.resolve(3)]).then(function(values) {
    log.push("all:" + values[0] + "," + values[1] + "," + values[2]);
});

Promise.resolve("pass").finally(function() {
    return thenable;
}).then(function(value) {
    log.push("finally-pass:" + value);
});

Promise.reject("orig").finally(function() {
    return Promise.resolve("cleanup");
}).catch(function(reason) {
    log.push("finally-reject:" + reason);
});

Promise.resolve("ignored").finally(function() {
    return Promise.reject("cleanup-fail");
}).catch(function(reason) {
    log.push("finally-cleanup:" + reason);
});

async function awaitThenable() {
    var value = await thenable;
    log.push("await:" + value);
}
awaitThenable();

async function awaitRejectedThenable() {
    try {
        await {
            then: function(resolve, reject) {
                reject("await-fail");
            }
        };
    } catch (reason) {
        log.push("await-reject:" + reason);
    }
}
awaitRejectedThenable();

setTimeout(function() {
    for (var i = 0; i < log.length; i++) {
        console.log(log[i]);
    }
}, 0);

0;
