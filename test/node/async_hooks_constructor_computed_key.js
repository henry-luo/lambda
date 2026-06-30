const assert = require('assert');
const async_hooks = require('async_hooks');

['init'].forEach((functionName) => {
  [null].forEach((nonFunction) => {
    assert.throws(() => {
      async_hooks.createHook({ [functionName]: nonFunction });
    }, {
      code: 'ERR_ASYNC_CALLBACK',
      name: 'TypeError',
      message: 'hook.' + functionName + ' must be a function',
    });
  });
});

console.log('async hooks computed key constructor ok');
