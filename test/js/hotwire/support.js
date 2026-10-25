export {assertions, collect, fixture, runSuite} from '../react/support.js';

export function until(predicate, description) {
  if (predicate()) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const observer = new MutationObserver(check);
    const timer = setTimeout(() => {
      observer.disconnect();
      reject(new Error('Timed out waiting for ' + description));
    }, 5000);
    function check() {
      if (!predicate()) return;
      observer.disconnect();
      clearTimeout(timer);
      resolve();
    }
    observer.observe(document.documentElement, {subtree: true, childList: true, attributes: true, characterData: true});
    queueMicrotask(check);
  });
}

export function eventOnce(target, name) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      target.removeEventListener(name, listener);
      reject(new Error('Timed out waiting for ' + name));
    }, 5000);
    function listener(event) {
      clearTimeout(timer);
      target.removeEventListener(name, listener);
      resolve(event);
    }
    target.addEventListener(name, listener);
  });
}
