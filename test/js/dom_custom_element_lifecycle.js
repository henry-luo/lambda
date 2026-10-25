async function custom_element_lifecycle() {
  function check(name, condition) {
    if (!condition) throw new Error(name);
    console.log(name + ': pass');
  }
  const host = document.createElement('div');
  host.innerHTML = '<test-lifecycle value="initial"></test-lifecycle>';
  document.body.appendChild(host);
  const existing = host.firstElementChild;
  const events = [];
  let constructed = 0;
  let setting = false, synchronous = false;
  class Lifecycle extends HTMLElement {
    static observedAttributes = ['value'];
    #token = 7;
    constructor() { super(); this.created = ++constructed; }
    connectedCallback() { events.push(['connected', this, this.#token]); }
    disconnectedCallback() { events.push(['disconnected', this, this.#token]); }
    attributeChangedCallback(name, oldValue, value) { events.push([name, this, oldValue, value]); if (value === 'next') synchronous = setting; }
  }
  customElements.define('test-lifecycle', Lifecycle);
  check('upgrade retains existing node', existing instanceof Lifecycle && existing.created === 1 && host.firstElementChild === existing);
  const created = document.createElement('test-lifecycle');
  const direct = new Lifecycle();
  check('constructors initialize native nodes', created instanceof Lifecycle && created.created === 2 && direct.created === 3 && direct.localName === 'test-lifecycle');
  await new Promise(resolve => setTimeout(resolve, 0));
  check('upgrade observes initial attributes', events.some(e => e[0] === 'value' && e[1] === existing && e[2] === null && e[3] === 'initial'));
  check('connection sees initialized private fields', events.some(e => e[0] === 'connected' && e[1] === existing && e[2] === 7));
  setting = true;
  existing.setAttribute('value', 'next');
  setting = false;
  check('attribute reactions respect caller guards', synchronous);
  existing.removeAttribute('value');
  await new Promise(resolve => setTimeout(resolve, 0));
  check('observed attribute transitions', events.some(e => e[0] === 'value' && e[2] === 'initial' && e[3] === 'next') && events.some(e => e[0] === 'value' && e[2] === 'next' && e[3] === null));
  existing.remove();
  host.appendChild(existing);
  await new Promise(resolve => setTimeout(resolve, 0));
  check('disconnect and reconnect preserve identity', events.filter(e => e[0] === 'disconnected' && e[1] === existing).length === 1 && events.filter(e => e[0] === 'connected' && e[1] === existing).length === 2 && existing.created === 1);
  const template = document.createElement('template');
  template.innerHTML = '<test-lifecycle value="inert"></test-lifecycle>';
  check('template custom elements stay inert', constructed === 3);
  const imported = document.importNode(template.content.firstElementChild, true);
  check('import upgrades into current registry', imported instanceof Lifecycle && imported.created === 4);
  const cloned = existing.cloneNode(true);
  check('clone constructs independent state', cloned instanceof Lifecycle && cloned.created === 5 && cloned !== existing);
  if (typeof gc === 'function') gc();
  host.append(created, imported, cloned);
  await new Promise(resolve => setTimeout(resolve, 0));
  check('native reactions retain receivers across gc', events.filter(e => e[0] === 'connected' && e[2] === 7).length === 5);
  host.remove();
  console.log('CUSTOM_ELEMENT_LIFECYCLE_DONE');
}
custom_element_lifecycle().catch(error => { console.log('CUSTOM_ELEMENT_LIFECYCLE_FAILED', error.message); throw error; });
