// S12.1.4: the transitive effect classifies the value, while the body collects.
function inner() {
    let started = clock()
    1
    2
}
function outer() { inner() }
[inner is pn, inner is fn, inner is function]
function() {
    outer()
}
