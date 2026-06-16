function DerivedPromise(executor) {
    executor(
        function(value) {
            console.log("resolved " + value);
        },
        function(reason) {
            console.log("rejected " + reason);
        }
    );
}

DerivedPromise.prototype = Object.create(Promise.prototype);
DerivedPromise.prototype.constructor = DerivedPromise;

new DerivedPromise(function(resolve, reject) {
    resolve(42);
});

