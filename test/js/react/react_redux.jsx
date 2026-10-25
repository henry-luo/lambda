import React from 'react';
import {flushSync} from 'react-dom';
import {configureStore, createSlice} from '@reduxjs/toolkit';
import {Provider, connect, shallowEqual, useDispatch, useSelector, useStore} from 'react-redux';
import {assertions, collect, runSuite} from './support';
import {mount} from './react_support';
import {dependencies} from './package.json';

const check = assertions('React Redux');

function main() {
  const slice = createSlice({name: 'counter', initialState: {count: 0, label: 'first'},
    reducers: {
      add(state, action) {state.count += action.payload;},
      label(state, action) {state.label = action.payload;}
    }});
  function trackedStore(count) {
    const store = configureStore({reducer: slice.reducer, preloadedState: {count, label: 'first'}});
    let subscriptions = 0;
    const subscribe = store.subscribe;
    store.subscribe = listener => {
      subscriptions++;
      const off = subscribe(listener);
      return () => {subscriptions--; off();};
    };
    return {store, active: () => subscriptions};
  }
  const first = trackedStore(0);
  const second = trackedStore(100);
  let renders = 0;
  let equalRenders = 0;
  let seenStore;
  function Counter() {
    renders++;
    const value = useSelector(state => state.count);
    const dispatch = useDispatch();
    seenStore = useStore();
    return <button id="redux-counter" onClick={() => dispatch(slice.actions.add(1))}>{value}</button>;
  }
  function Equal() {
    equalRenders++;
    const selected = useSelector(state => ({count: state.count}), shallowEqual);
    return <span id="redux-equal">{selected.count}</span>;
  }
  const Connected = connect(state => ({value: state.count}), {add: slice.actions.add})(
    function ConnectedCounter({value, add, label}) {
      return <button id="redux-connected" onClick={() => add(5)}>{label + ':' + value}</button>;
    });
  function Nested() { return <span id="redux-nested">{useSelector(state => state.count)}</span>; }
  const view = mount(<Provider store={first.store}>
    <Counter/><Equal/><Connected label="connected"/>
    <Provider store={second.store}><Nested/></Provider>
  </Provider>);
  const byId = id => view.container.querySelector('#redux-' + id);
  check('Provider and store hooks', byId('counter').textContent === '0' && seenStore === first.store && first.active() === 1);
  check('nested Provider isolation', byId('nested').textContent === '100' && second.active() === 1);
  collect();
  flushSync(() => byId('counter').click());
  check('dispatch hook and DOM update', first.store.getState().count === 1 && byId('counter').textContent === '1' && byId('equal').textContent === '1');
  const before = renders;
  const equalBefore = equalRenders;
  flushSync(() => first.store.dispatch(slice.actions.label('changed')));
  check('selector equality skips unrelated updates', renders === before && equalRenders === equalBefore && byId('counter').textContent === '1');
  flushSync(() => { first.store.dispatch(slice.actions.add(1)); first.store.dispatch(slice.actions.add(1)); });
  check('batched store notifications', renders === before + 1 && equalRenders === equalBefore + 1 && byId('counter').textContent === '3');
  flushSync(() => byId('connected').click());
  check('connect state and bound actions', byId('connected').textContent === 'connected:8' && byId('counter').textContent === '8' && byId('nested').textContent === '100');
  flushSync(() => second.store.dispatch(slice.actions.add(2)));
  check('independent store updates', byId('nested').textContent === '102' && byId('counter').textContent === '8');
  view.render(<Provider store={second.store}><Counter/></Provider>);
  check('Provider store replacement', first.active() === 0 && second.active() === 1 && seenStore === second.store && byId('counter').textContent === '102');
  collect();
  flushSync(() => first.store.dispatch(slice.actions.add(50)));
  check('old store unsubscribed', byId('counter').textContent === '102');
  view.unmount();
  collect();
  second.store.dispatch(slice.actions.add(1));
  check('unmount subscription cleanup', first.active() === 0 && second.active() === 0 && view.container.textContent === '');
}

runSuite('React Redux', dependencies['react-redux'], main);
