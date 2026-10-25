import {configureStore, createSlice, createSelector, createEntityAdapter,
  createAsyncThunk, createListenerMiddleware} from '@reduxjs/toolkit';
import {assertions, collect, runSuite} from './support';
import {dependencies} from './package.json';

const check = assertions('Redux Toolkit');

async function main() {
  const slice = createSlice({
    name: 'counter',
    initialState: {value: 0, items: [{id: 'a', value: 1}], stable: {label: 'retained'}},
    reducers: {
      add(state, action) { state.value += action.payload; },
      edit(state, action) { state.items[0].value = action.payload; },
      append: {
        prepare(label) { return {payload: {id: label, value: label.length}}; },
        reducer(state, action) { state.items.push(action.payload); }
      }
    }
  });
  const order = [];
  const store = configureStore({
    reducer: slice.reducer,
    middleware: defaults => defaults().concat(api => next => action => {
      order.push('before:' + action.type + ':' + api.getState().value);
      const result = next(action);
      order.push('after:' + action.type + ':' + api.getState().value);
      return result;
    })
  });
  check('store and generated actions', store.getState().value === 0 && slice.actions.add.type === 'counter/add' && slice.actions.add.match(slice.actions.add(1)));
  const initial = store.getState();
  store.dispatch(slice.actions.edit(9));
  const edited = store.getState();
  check('Immer nested updates and structural sharing', initial.items[0].value === 1 && edited.items[0].value === 9 && edited !== initial && edited.items !== initial.items && edited.stable === initial.stable);
  check('Immer frozen snapshots', Object.isFrozen(initial) && Object.isFrozen(edited.items) && Object.isFrozen(edited.items[0]));
  store.dispatch(slice.actions.append('second'));
  check('prepared actions and array drafts', store.getState().items[1].id === 'second' && store.getState().items[1].value === 6);
  let notifications = 0;
  const unsubscribe = store.subscribe(() => notifications++);
  order.length = 0;
  store.dispatch(slice.actions.add(2));
  unsubscribe();
  store.dispatch(slice.actions.add(3));
  check('subscriptions and middleware ordering', notifications === 1 && store.getState().value === 5 && order.join(',') === 'before:counter/add:0,after:counter/add:2,before:counter/add:2,after:counter/add:5');
  const selectValues = createSelector([state => state.items], items => items.map(item => item.value));
  const selected = selectValues(store.getState());
  store.dispatch(slice.actions.add(1));
  check('memoized selectors', selected === selectValues(store.getState()) && selected.join(',') === '9,6' && selectValues.recomputations() === 1);
  store.dispatch((dispatch, getState) => dispatch(slice.actions.add(getState().value)));
  check('thunk middleware', store.getState().value === 12);

  const adapter = createEntityAdapter({sortComparer: (a, b) => a.rank - b.rank});
  const entities = createSlice({
    name: 'entities', initialState: adapter.getInitialState(),
    reducers: {upsert: adapter.upsertMany, update: adapter.updateOne, remove: adapter.removeOne}
  });
  const entityStore = configureStore({reducer: entities.reducer});
  entityStore.dispatch(entities.actions.upsert([{id: 'b', rank: 2}, {id: 'a', rank: 1}]));
  entityStore.dispatch(entities.actions.update({id: 'b', changes: {rank: 0}}));
  const selectors = adapter.getSelectors();
  check('entity adapter sorting and updates', selectors.selectIds(entityStore.getState()).join(',') === 'b,a' && selectors.selectById(entityStore.getState(), 'b').rank === 0);
  entityStore.dispatch(entities.actions.remove('a'));
  check('entity adapter removal', selectors.selectTotal(entityStore.getState()) === 1 && selectors.selectById(entityStore.getState(), 'a') === undefined);

  const load = createAsyncThunk('load', async (value, api) => {
    await Promise.resolve();
    if (value < 0) return api.rejectWithValue('negative');
    return value * 2;
  });
  const asyncSlice = createSlice({
    name: 'async', initialState: {status: 'idle', value: 0, error: null}, reducers: {},
    extraReducers: builder => builder
      .addCase(load.pending, state => {state.status = 'pending';})
      .addCase(load.fulfilled, (state, action) => {state.status = 'done'; state.value = action.payload;})
      .addCase(load.rejected, (state, action) => {state.status = 'failed'; state.error = action.payload;})
  });
  const asyncStore = configureStore({reducer: asyncSlice.reducer});
  const pending = asyncStore.dispatch(load(7));
  check('async thunk pending', asyncStore.getState().status === 'pending' && typeof pending.requestId === 'string');
  collect();
  const fulfilled = await pending;
  check('async thunk fulfillment and unwrap', load.fulfilled.match(fulfilled) && await pending.unwrap() === 14 && asyncStore.getState().value === 14 && asyncStore.getState().status === 'done');
  let rejection;
  try { await asyncStore.dispatch(load(-1)).unwrap(); } catch (error) { rejection = error; }
  check('async thunk rejected values', rejection === 'negative' && asyncStore.getState().error === 'negative' && asyncStore.getState().status === 'failed');
  const wait = createAsyncThunk('wait', () => new Promise(() => {}));
  const canceled = asyncStore.dispatch(wait());
  canceled.abort('test cancellation');
  const aborted = await canceled;
  check('async thunk cancellation', aborted.meta.aborted && aborted.error.name === 'AbortError' && aborted.error.message === 'test cancellation');

  const listener = createListenerMiddleware();
  const observed = [];
  const stop = listener.startListening({actionCreator: slice.actions.add,
    effect: (action, api) => observed.push(api.getOriginalState().value + ':' + api.getState().value)});
  const listened = configureStore({reducer: slice.reducer, middleware: defaults => defaults().prepend(listener.middleware)});
  collect();
  listened.dispatch(slice.actions.add(4));
  stop();
  listened.dispatch(slice.actions.add(1));
  check('listener middleware and removal', observed.join(',') === '0:4' && listened.getState().value === 5);
}

runSuite('Redux Toolkit', dependencies['@reduxjs/toolkit'], main);
