import { __webpack_require__ as e } from "./rspack-runtime.mjs";
import * as o from "./rspack-sidecar.mjs";

e.m[1] = function(module, exports, o) {
    o.a(module, async function(defer, done) {
        done();
    }, 1);
};
e.m[2] = function(module, exports, o) {
    o.a(module, async function(defer, done) {
        done();
    }, 1);
};
e.m[3] = function(module, exports, o) {
    o.a(module, async function(defer, done) {
        var first = o(1);
        var second = o(2);
        var dependencies = defer([first, second]);
        [first, second] = dependencies.then ? (await dependencies)() : dependencies;
        done();
    }, 1);
};

export const result = e(3);
