const cases = [
    ['window properties resolve as identifiers',
        'window.libraryReady=42;', 'libraryReady===42'],
    ['global var is visible on window',
        'var libraryReady=42;', 'window.libraryReady===42'],
    ['lexical bindings remain distinct from window properties',
        'window.jQuery=7;const jQuery=42;', 'jQuery===42&&window.jQuery===7'],
    ['library exports do not create unrelated aliases',
        'window.jQuery=function(){return 42};', 'jQuery()===42&&typeof $==="undefined"&&typeof window.$==="undefined"'],
    ['lexical declarations do not become window exports',
        'const jQuery=42;', 'jQuery===42&&!Object.prototype.hasOwnProperty.call(window,"jQuery")'],
    ['host does not read unobserved global accessors',
        'var calls=0;Object.defineProperty(window,"jQuery",{get(){calls++;return 42}});', 'calls===0'],
    ['throwing unobserved accessor does not stop later scripts',
        'Object.defineProperty(window,"jQuery",{get(){throw new Error("unobserved")}});', 'true'],
    ['authored aliases remain ordinary global properties',
        'window.jQuery=window.$=function(){return 42};', '$()===42&&jQuery()===42&&window.$===window.jQuery'],
    ['global function declarations are window properties',
        'function libraryReady(){return 42}', 'libraryReady()===42&&window.libraryReady===libraryReady'],
    ['named lexical functions are not window exports',
        'const privateMethod=function privateMethod(){return 42};',
        'privateMethod()===42&&!Object.prototype.hasOwnProperty.call(window,"privateMethod")'],
    ['named lexical functions do not invoke global setters',
        'var writes=0;Object.defineProperty(window,"privateMethod",{set(value){writes++},get(){return 7}});' +
        'const privateMethod=function privateMethod(){return 42};', 'privateMethod()===42&&writes===0'],
];

function fixtureHtml(setup, condition) {
    return '<!doctype html><style>body{margin:0;background:red}</style><body><script>' + setup +
        '</script><script>document.addEventListener("DOMContentLoaded",function(){if(' + condition +
        ')document.body.style.background="lime"});</script></body>';
}
module.exports = {cases, fixtureHtml};
