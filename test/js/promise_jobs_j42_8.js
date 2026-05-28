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

var guardThenable = {
    then: function(resolve, reject) {
        log.push("guard-thenable-job");
        resolve("guard-ok");
    }
};
new Promise(function(resolve, reject) {
    resolve(guardThenable);
    reject("guard-bad");
}).then(function(value) {
    log.push("resolver-guard:" + value);
}).catch(function(reason) {
    log.push("resolver-guard-bad:" + reason);
});

var throwAfterResolve = {
    then: function(resolve, reject) {
        log.push("throw-after-resolve");
        resolve("tar-ok");
        throw "tar-bad";
    }
};
Promise.resolve(throwAfterResolve).then(function(value) {
    log.push("throw-after-resolve-result:" + value);
}).catch(function(reason) {
    log.push("throw-after-resolve-bad:" + reason);
});

function CustomPromise(executor) {
    log.push("custom-capability");
    return new Promise(executor);
}
CustomPromise.resolve = Promise.resolve.bind(Promise);
Promise.resolve.call(CustomPromise, guardThenable).then(function(value) {
    log.push("custom-resolve:" + value);
});
Promise.reject.call(CustomPromise, "custom-fail").catch(function(reason) {
    log.push("custom-reject:" + reason);
});
Promise.all.call(CustomPromise, [Promise.resolve("custom-a"), guardThenable]).then(function(values) {
    log.push("custom-all:" + values[0] + "," + values[1]);
});

Promise.all([1, thenable, Promise.resolve(3)]).then(function(values) {
    log.push("all:" + values[0] + "," + values[1] + "," + values[2]);
});

var doubleAll = Promise.resolve("unused");
doubleAll.then = function(resolve, reject) {
    log.push("all-double");
    resolve("first");
    resolve("second");
    reject("bad");
};
Promise.all([doubleAll, Promise.resolve("later")]).then(function(values) {
    log.push("all-double-result:" + values[0] + "," + values[1]);
});

var doubleAny = Promise.resolve("unused");
doubleAny.then = function(resolve, reject) {
    log.push("any-double");
    reject("first-reject");
    resolve("late-fulfill");
};
Promise.any([doubleAny, Promise.reject("second-reject")]).then(function(value) {
    log.push("any-double-fulfill:" + value);
}).catch(function(error) {
    log.push("any-double-reject:" + error.name + "," + error.errors[0] + "," + error.errors[1]);
});

var doubleSettled = Promise.resolve("unused");
doubleSettled.then = function(resolve, reject) {
    log.push("settled-double");
    resolve("settled-first");
    reject("settled-late");
};
Promise.allSettled([doubleSettled, Promise.reject("settled-second")]).then(function(results) {
    log.push("settled-double-result:" + results[0].status + "," + results[0].value + "," + results[1].status + "," + results[1].reason);
});

var doubleRace = Promise.resolve("unused");
doubleRace.then = function(resolve, reject) {
    log.push("race-double");
    resolve(guardThenable);
    reject("race-bad");
};
Promise.race([doubleRace]).then(function(value) {
    log.push("race-double-result:" + value);
}).catch(function(reason) {
    log.push("race-double-bad:" + reason);
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
