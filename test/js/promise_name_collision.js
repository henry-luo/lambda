var PDFJS = {};
var Promise = PDFJS.Promise = (function() {
    function Promise(name, data) {
        this.name = name || "anonymous";
        this.data = data || null;
    }

    Promise.prototype.resolve = function(data) {
        this.data = data;
        return data;
    };

    return Promise;
})();

var pagePromise = new PDFJS.Promise("Page 1");
console.log(pagePromise.name);

var localPromise = new Promise();
console.log(localPromise.resolve("legacy"));
