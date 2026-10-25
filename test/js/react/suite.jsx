import React, {Component, Suspense, createContext, lazy, startTransition,
  useContext, useEffect, useLayoutEffect, useState} from 'react';
import {createRoot, hydrateRoot} from 'react-dom/client';
import {createPortal, flushSync} from 'react-dom';

function check(name, condition) {
  if (!condition) throw new Error('React compatibility: ' + name);
  console.log(name + ': pass');
}

function fixture() {
  const element = document.createElement('div');
  document.body.appendChild(element);
  return element;
}

function collect() {
  if (typeof gc === 'function') gc();
}

async function client() {
  const container = fixture();
  const portal = fixture();
  const Theme = createContext('default');
  const actions = {};
  const events = [];
  let renders = 0;
  let effects = 0;
  let cleanups = 0;
  let reference;
  let committed;
  function App() {
    const [count, setCount] = useState(0);
    const [text, setText] = useState('initial');
    const [checked, setChecked] = useState(false);
    const [radio, setRadio] = useState('a');
    const [selected, setSelected] = useState('b');
    const [area, setArea] = useState('note');
    const [items, setItems] = useState(['a', 'b', 'c']);
    actions.count = setCount;
    actions.items = setItems;
    renders++;
    useEffect(() => { effects++; return () => { cleanups++; }; }, [count]);
    useLayoutEffect(() => { if (committed) { const resolve = committed; committed = null; resolve(); } });
    return <section onClickCapture={() => events.push('capture')} onClick={() => events.push('bubble')}>
      <span id="react-count" className={count ? 'updated' : 'initial'} style={{color: count ? 'green' : 'red'}}>{count}</span>
      <span id="react-theme">{useContext(Theme)}</span>
      <button id="react-button" ref={node => {reference = node;}} onClick={() => setCount(c => c + 1)}>increment</button>
      <input id="react-text" value={text} onChange={event => setText(event.target.value)}/>
      <input id="react-check" type="checkbox" checked={checked} onChange={event => {events.push('checked:' + event.target.checked); setChecked(event.target.checked);}}/>
      {['a', 'b'].map(value => <input key={value} id={'react-radio-' + value} type="radio" name="react-group" checked={radio === value} onChange={() => setRadio(value)}/>)}
      <select id="react-select" value={selected} onChange={event => setSelected(event.target.value)}><option value="a">A</option><option value="b">B</option></select>
      <textarea id="react-area" value={area} onChange={event => setArea(event.target.value)}/>
      <span id="react-model">{text + ':' + checked + ':' + radio + ':' + selected + ':' + area}</span>
      <ul id="react-list">{items.map(id => <li id={'react-item-' + id} key={id}>{id}</li>)}</ul>
      {createPortal(<button id="react-portal" onClick={() => events.push('portal')}>portal</button>, portal)}
    </section>;
  }
  const root = createRoot(container);
  flushSync(() => root.render(<Theme value="dark"><App/></Theme>));
  const byId = id => document.getElementById('react-' + id);
  check('mount/context/effects', byId('count').textContent === '0' && byId('theme').textContent === 'dark' && effects === 1);
  check('ref identity', reference === byId('button'));
  collect();
  flushSync(() => byId('button').click());
  check('delegated click', byId('count').textContent === '1' && events.join(',') === 'capture,bubble');
  check('style mutation', byId('count').className === 'updated' && byId('count').style.color === 'green');
  const before = renders;
  flushSync(() => { actions.count(c => c + 1); actions.count(c => c + 1); });
  check('batched functional updates', byId('count').textContent === '3' && renders === before + 1 && effects === 3 && cleanups === 2);
  const first = byId('item-a');
  flushSync(() => actions.items(['c', 'a', 'b']));
  check('keyed identity', byId('list').textContent === 'cab' && byId('item-a') === first);
  events.length = 0;
  flushSync(() => byId('portal').click());
  check('portal propagation', events.join(',') === 'capture,portal,bubble');
  const text = byId('text');
  const value = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
  const checked = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'checked');
  check('native input accessors', typeof value.get === 'function' && typeof value.set === 'function' && typeof checked?.get === 'function' && typeof checked?.set === 'function');
  flushSync(() => { value.set.call(text, 'edited'); text.dispatchEvent(new Event('input', {bubbles: true})); });
  check('controlled text', byId('model').textContent.startsWith('edited:') && text.value === 'edited');
  events.length = 0;
  flushSync(() => byId('check').click());
  check('controlled checkbox', byId('check').checked && byId('model').textContent.startsWith('edited:true:') && events.join(',') === 'capture,bubble,checked:true');
  flushSync(() => byId('radio-b').click());
  check('controlled radio', !byId('radio-a').checked && byId('radio-b').checked && byId('model').textContent.startsWith('edited:true:b:'));
  check('initial select/textarea', byId('select').value === 'b' && byId('area').value === 'note');
  flushSync(() => { byId('select').value = 'a'; byId('select').dispatchEvent(new Event('change', {bubbles: true})); });
  const areaValue = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value');
  flushSync(() => { areaValue.set.call(byId('area'), 'edited note'); byId('area').dispatchEvent(new Event('input', {bubbles: true})); });
  check('controlled select/textarea', byId('model').textContent === 'edited:true:b:a:edited note');
  async function update(schedule) {
    await new Promise(resolve => { committed = resolve; schedule(() => actions.count(c => c + 1)); });
  }
  await update(callback => Promise.resolve().then(callback));
  await update(callback => setTimeout(callback, 0));
  const channel = new MessageChannel();
  await update(callback => { channel.port1.onmessage = callback; channel.port2.postMessage(null); });
  channel.port1.close(); channel.port2.close();
  await update(callback => startTransition(callback));
  check('scheduled updates', byId('count').textContent === '7');
  collect();
  flushSync(() => root.unmount());
  check('unmount cleanup', reference === null && container.textContent === '' && portal.textContent === '' && effects === cleanups);
  container.remove(); portal.remove();
  collect();
}

async function hydration() {
  const container = fixture();
  container.innerHTML = '<button id="react-hydrated">0</button>';
  const original = container.firstChild;
  let mounted;
  const ready = new Promise(resolve => {mounted = resolve;});
  function App() {
    const [count, setCount] = useState(0);
    useLayoutEffect(() => mounted(), []);
    return <button id="react-hydrated" onClick={() => setCount(c => c + 1)}>{count}</button>;
  }
  let errors = 0;
  const root = hydrateRoot(container, <App/>, {onRecoverableError() {errors++;}});
  await ready;
  collect();
  flushSync(() => original.click());
  check('hydration identity/update', errors === 0 && original === container.firstChild && original.textContent === '1');
  flushSync(() => root.unmount());
  container.remove();
}

async function suspense() {
  const container = fixture();
  let loaded;
  let committed;
  const ready = new Promise(resolve => {committed = resolve;});
  const module = new Promise(resolve => {loaded = resolve;});
  const Child = lazy(() => module);
  function Content() { useLayoutEffect(() => committed(), []); return <span>loaded</span>; }
  const root = createRoot(container);
  flushSync(() => root.render(<Suspense fallback={<span>pending</span>}><Child/></Suspense>));
  check('suspense fallback', container.textContent === 'pending');
  loaded({default: Content});
  await ready;
  check('lazy resolution', container.textContent === 'loaded');
  flushSync(() => root.unmount());
  container.remove();
}

function boundary() {
  const container = fixture();
  let caught = '';
  class Boundary extends Component {
    constructor(props) { super(props); this.state = {failed: false}; }
    static getDerivedStateFromError() { return {failed: true}; }
    render() { return this.state.failed ? <span>recovered</span> : this.props.children; }
  }
  function Broken() { throw new Error('expected boundary'); }
  const root = createRoot(container, {onCaughtError(error) {caught = error.message;}});
  flushSync(() => root.render(<Boundary><Broken/></Boundary>));
  check('class error boundary', caught === 'expected boundary' && container.textContent === 'recovered');
  flushSync(() => root.unmount());
  container.remove();
}

function interactive(container) {
  function App() {
    const [count, setCount] = useState(0);
    const [checked, setChecked] = useState(false);
    return <div>
      <button id="react-live-button" onClick={() => setCount(c => c + 1)}>increment</button>
      <span id="react-live-count">{count}</span>
      <input id="react-live-check" type="checkbox" checked={checked} onChange={event => setChecked(event.target.checked)}/>
      <span id="react-live-check-state">{checked ? 'checked' : 'unchecked'}</span>
    </div>;
  }
  flushSync(() => createRoot(container).render(<App/>));
}

async function main() {
  console.log('React ' + React.version);
  await client();
  await hydration();
  await suspense();
  boundary();
  collect();
  const physical = document.getElementById('react-physical-root');
  if (physical) interactive(physical);
  document.body.dataset.reactResult = 'pass';
  console.log('REACT_DONE');
}
main().catch(error => { console.log('REACT_FAILED', error.message); throw error; });
