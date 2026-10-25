import React from 'react';
import {flushSync} from 'react-dom';
import {create, useStore} from 'zustand';
import {createStore} from 'zustand/vanilla';
import {createJSONStorage, persist, subscribeWithSelector} from 'zustand/middleware';
import {useShallow} from 'zustand/react/shallow';
import {assertions, collect, runSuite} from './support';
import {mount} from './react_support';
import {dependencies} from './package.json';

const check = assertions('Zustand');

async function main() {
  const store = createStore(set => ({count: 0, label: 'stable', increment: () => set(state => ({count: state.count + 1}))}));
  const initial = store.getInitialState();
  const notifications = [];
  const off = store.subscribe((next, previous) => notifications.push(previous.count + ':' + next.count));
  store.getState().increment();
  store.setState({count: 2});
  check('vanilla store merge and actions', store.getState().count === 2 && store.getState().label === 'stable' && initial.count === 0 && store.getInitialState() === initial);
  store.setState(store.getState());
  check('subscriptions and identical-state bailout', notifications.join(',') === '0:1,1:2');
  off();
  store.setState({count: 3}, true);
  check('replace and unsubscribe', Object.keys(store.getState()).join(',') === 'count' && store.getState().count === 3 && notifications.length === 2);

  const selected = createStore(subscribeWithSelector(() => ({count: 0, other: 0})));
  const changes = [];
  const stop = selected.subscribe(state => state.count, (next, previous) => changes.push(previous + ':' + next), {fireImmediately: true});
  selected.setState({other: 1});
  selected.setState({count: 1});
  stop();
  selected.setState({count: 2});
  check('selector middleware and immediate subscription', changes.join(',') === '0:0,0:1');
  let parity = 0;
  const stopParity = selected.subscribe(state => ({parity: state.count % 2}), () => parity++, {equalityFn: (a, b) => a.parity === b.parity});
  selected.setState({count: 4});
  selected.setState({count: 5});
  stopParity();
  check('selector middleware custom equality', parity === 1);

  // custom storage is a supported persist adapter and keeps this test offline.
  const values = new Map();
  const storage = createJSONStorage(() => ({
    getItem: key => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
    removeItem: key => values.delete(key)
  }));
  function persisted() {
    return createStore(persist(set => ({count: 0, transient: 'fresh', add: () => set(state => ({count: state.count + 1}))}),
      {name: 'lambda-zustand', storage, partialize: state => ({count: state.count}), version: 1}));
  }
  const saved = persisted();
  saved.getState().add();
  saved.setState({transient: 'discarded'});
  const restored = persisted();
  check('persist serialization and hydration', restored.persist.hasHydrated() && restored.getState().count === 1 && restored.getState().transient === 'fresh' && typeof restored.getState().add === 'function' && JSON.parse(values.get('lambda-zustand')).version === 1);
  restored.setState({count: 8});
  await saved.persist.rehydrate();
  check('persist explicit rehydration', saved.getState().count === 8);
  saved.persist.clearStorage();
  check('persist storage cleanup', !values.has('lambda-zustand'));

  const bound = create(set => ({count: 0, label: 'stable', add: () => set(state => ({count: state.count + 1}))}));
  let countRenders = 0;
  let shallowRenders = 0;
  function Counter() {
    countRenders++;
    const count = bound(state => state.count);
    const add = bound(state => state.add);
    return <button id="zustand-count" onClick={add}>{count}</button>;
  }
  function Shallow() {
    shallowRenders++;
    const [count, add] = bound(useShallow(state => [state.count, state.add]));
    return <button id="zustand-shallow" onClick={add}>{count}</button>;
  }
  function Vanilla() { return <span id="zustand-vanilla">{useStore(selected, state => state.count)}</span>; }
  const view = mount(<><Counter/><Shallow/><Vanilla/></>);
  const byId = id => view.container.querySelector('#zustand-' + id);
  check('React bound and vanilla hooks', byId('count').textContent === '0' && byId('shallow').textContent === '0' && byId('vanilla').textContent === '5');
  collect();
  flushSync(() => byId('count').click());
  check('React event and store rerender', byId('count').textContent === '1' && byId('shallow').textContent === '1');
  const before = countRenders;
  const shallowBefore = shallowRenders;
  flushSync(() => bound.setState({label: 'changed'}));
  check('React selectors and shallow equality', countRenders === before && shallowRenders === shallowBefore);
  flushSync(() => { bound.getState().add(); bound.getState().add(); selected.setState({count: 9}); });
  check('batched external store updates', byId('count').textContent === '3' && byId('shallow').textContent === '3' && byId('vanilla').textContent === '9' && countRenders === before + 1);
  view.unmount();
  collect();
  const unmounted = countRenders;
  bound.getState().add();
  selected.setState({count: 10});
  check('React unmount cleanup', countRenders === unmounted && view.container.textContent === '');
}

runSuite('Zustand', dependencies.zustand, main);
