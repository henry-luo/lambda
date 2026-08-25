// A closure created inside `with` must not direct-dispatch a shadowed callee.
var target = function() { return "outer"; };
var capture;
with ({ target: function() { return "with"; } }) {
    capture = function() { return target(); };
}
console.log(capture());
