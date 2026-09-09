// JSCU29: await's resolved-value handoff has one exact realm root owner.
var gate = Promise.withResolvers();

async function wait_for_value() {
    var value = await gate.promise;
    return value.answer;
}

var result = wait_for_value();
gc();
gate.resolve({ answer: 42 });
result.then(function(value) {
    console.log(value);
});
