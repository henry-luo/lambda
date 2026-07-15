(function () {
  function make_handler(marker) {
    return function (event) {
      for (let { target } = event; target && target !== this; target = target.parentNode) {
        if (target.value) return marker + target.value;
      }
      return 'missing';
    };
  }

  const handler = make_handler('');
  console.log(handler.call({}, { target: { parentNode: { value: 'ok', parentNode: null } } }));
  setTimeout(function () {
    console.log('later:' + handler.call({}, { target: { parentNode: { value: 'async', parentNode: null } } }));
  }, 0);
})();
