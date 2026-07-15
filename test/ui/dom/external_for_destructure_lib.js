(function () {
  function delegated_handler(element, selector, callback) {
    return function (event) {
      const matches = element.querySelectorAll(selector);
      for (let {
        target
      } = event; target && target !== this; target = target.parentNode) {
        for (const match of matches) {
          if (match !== target) continue;
          callback.call(target, event);
          return;
        }
      }
    };
  }

  function install_delegated_handler(element, selector, callback) {
    const handler = delegated_handler(element, selector, callback);
    element.addEventListener('click', handler);
  }

  window.onerror = function () {
    document.body.classList.add('delegated-error');
  };
  install_delegated_handler(document, '.absent', function () {
    document.body.classList.add('delegated-hit');
  });
  document.addEventListener('click', function () {
    document.body.classList.add('dispatch-continued');
    if (target === 'earlier-script-binding') {
      document.body.classList.add('outer-target-preserved');
    }
  });
})();
