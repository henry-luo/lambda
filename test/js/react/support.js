export function assertions(library) {
  return function check(name, condition) {
    if (!condition) throw new Error(library + ' compatibility: ' + name);
    console.log(name + ': pass');
  };
}

export function fixture() {
  const element = document.createElement('div');
  document.body.appendChild(element);
  return element;
}

export function collect() {
  if (typeof gc === 'function') gc();
}

export function runSuite(library, version, main) {
  const marker = library.toUpperCase().replace(/ /g, '_');
  console.log(library + ' ' + version);
  Promise.resolve().then(main).then(() => {
    collect();
    console.log(marker + '_DONE');
  }).catch(error => {
    console.log(marker + '_FAILED', error.message);
    throw error;
  });
}
