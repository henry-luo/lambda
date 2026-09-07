// a separate script must preserve this IIFE parameter over a same-named
// retained preamble binding after its creating call returns.
(function ($) {
    window.iifeCapturedValue = function () {
        return $(41);
    };
})(function (value) {
    return value + 1;
});

console.log(iifeCapturedValue());
