const { AsyncLocalStorage } = require('async_hooks');

const als = new AsyncLocalStorage();

als.run('timer-store', () => {
  setTimeout((left, right) => {
    console.log(als.getStore() + ':' + left + right);
  }, 0, 'a', 'b');
});

console.log(als.getStore());
