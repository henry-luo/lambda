// Wide scalar values stored beyond their producing activation must use an
// owned payload slot, so a later throw or allocation cannot clobber them.
let caught;
try {
    throw 5e-324;
} catch (first) {
    caught = first;
    try {
        throw 1e-323;
    } catch (second) {
        void second;
    }
}

function strictThis() {
    "use strict";
    return this;
}

const bound = strictThis.bind(5e-324);
const churn = [];
for (let i = 0; i < 1024; i++) churn.push({ i: i });

const callback_value = [0].map(function () {
    return 5e-324;
})[0];

// The reducer forwards its callback result through two native dispatch helpers.
const reduced_value = [0].reduce(function () {
    return 5e-324;
}, 0);

console.log(caught === 5e-324, bound() === 5e-324,
    callback_value === 5e-324, reduced_value === 5e-324);
