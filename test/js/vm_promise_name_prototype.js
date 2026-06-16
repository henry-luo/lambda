const vm = require('vm');
const ctx = { print: console.log, console };
vm.createContext(ctx);

vm.runInContext(`
var PDFJS = {};
var Promise = PDFJS.Promise = (function() {
    function Promise(name) {
        this.name = name;
    }

    Promise.prototype = {
        resolve: function(value) {
            this.value = value;
            console.log("resolved " + value);
        }
    };

    return Promise;
})();

var promise = new PDFJS.Promise("doc");
console.log(typeof PDFJS.Promise.prototype.resolve);
console.log(typeof promise.resolve);
promise.resolve("ok");
`, ctx);
