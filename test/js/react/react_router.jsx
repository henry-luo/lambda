import React, {useLayoutEffect} from 'react';
import {MemoryRouter, Routes, Route, Outlet, Link, NavLink,
  useLocation, useNavigate, useParams, useSearchParams, useOutletContext,
  createMemoryRouter, useLoaderData, useActionData, useRouteError,
  isRouteErrorResponse, redirect, matchRoutes, matchPath, generatePath} from 'react-router';
import {RouterProvider} from 'react-router/dom';
import {assertions, collect, runSuite} from './support';
import {mount} from './react_support';
import {dependencies} from './package.json';

const check = assertions('React Router');

// completion follows the destination's committed DOM, including transitions.
function navigationObserver() {
  let pending;
  function Commit() {
    const location = useLocation();
    useLayoutEffect(() => {
      if (pending && location.pathname + location.search === pending.path) {
        const resolve = pending.resolve;
        pending = null;
        resolve();
      }
    });
    return null;
  }
  return {
    Commit,
    change(path, action) {
      return new Promise((resolve, reject) => {
        pending = {path, resolve};
        Promise.resolve(action()).catch(reject);
      });
    }
  };
}

async function declarative() {
  const observer = navigationObserver();
  const Commit = observer.Commit;
  let navigate;
  let location;
  let search;
  function Shell() {
    navigate = useNavigate();
    location = useLocation();
    return <><Commit/><span id="router-path">{location.pathname + location.search}</span><Outlet context="nested"/></>;
  }
  function User() {
    const {id} = useParams();
    const [params, setParams] = useSearchParams();
    search = setParams;
    return <section>
      <span id="router-user">{id + ':' + params.get('tab') + ':' + useOutletContext()}</span>
      <Link id="router-link" to="/users/9?tab=activity">next</Link>
      <Link id="router-canceled" to="/missing" onClick={event => event.preventDefault()}>canceled</Link>
      <NavLink id="router-active" to={'/users/' + id}>active</NavLink>
    </section>;
  }
  const view = mount(<MemoryRouter initialEntries={['/start', '/users/42?tab=info']} initialIndex={1}>
    <Routes><Route path="/" element={<Shell/>}>
      <Route path="users/:id" element={<User/>}/>
      <Route path="*" element={<span id="router-missing">missing</span>}/>
    </Route></Routes>
  </MemoryRouter>);
  const byId = id => view.container.querySelector('#router-' + id);
  check('nested routes params search and outlet context', byId('user').textContent === '42:info:nested' && byId('path').textContent === '/users/42?tab=info');
  check('NavLink active semantics', byId('active').getAttribute('aria-current') === 'page' && byId('active').className.includes('active'));
  byId('canceled').click();
  check('canceled link navigation', byId('path').textContent === '/users/42?tab=info');
  collect();
  await observer.change('/users/9?tab=activity', () => byId('link').click());
  check('Link navigation', byId('user').textContent === '9:activity:nested');
  await observer.change('/users/9?tab=settings', () => search(params => {params.set('tab', 'settings'); return params;}, {replace: true}));
  check('search parameter update', byId('user').textContent === '9:settings:nested');
  await observer.change('/users/7?tab=state', () => navigate('/users/7?tab=state', {replace: true, state: {source: 'test'}}));
  check('replace navigation and location state', byId('user').textContent === '7:state:nested' && location.state.source === 'test');
  await observer.change('/users/42?tab=info', () => navigate(-1));
  check('history back after replace', byId('user').textContent === '42:info:nested' && location.state === null);
  await observer.change('/missing', () => navigate('/missing'));
  check('wildcard route', byId('missing').textContent === 'missing' && byId('user') === null);
  view.unmount();
}

async function dataRouter() {
  const observer = navigationObserver();
  const Commit = observer.Commit;
  let loads = 0;
  let slowStarted;
  let release;
  let slowSignal;
  const started = new Promise(resolve => {slowStarted = resolve;});
  function Shell() { return <><Commit/><Outlet/></>; }
  function User() { const value = useLoaderData(); return <span id="router-data">{value.id + ':' + value.mode}</span>; }
  function Editor() { const value = useActionData(); return <span id="router-action">{value ? value.saved : 'empty'}</span>; }
  function ErrorPage() {
    const error = useRouteError();
    return <span id="router-error">{isRouteErrorResponse(error) ? error.status + ':' + error.data : error.message}</span>;
  }
  const router = createMemoryRouter([{path: '/', element: <Shell/>, children: [
    {index: true, element: <span>home</span>},
    {path: 'users/:id', element: <User/>, loader: async ({params, request}) => {
      loads++;
      await Promise.resolve();
      return {id: params.id, mode: new URL(request.url).searchParams.get('mode') || 'default'};
    }},
    {path: 'redirect', loader: () => redirect('/users/99'), element: <span>redirecting</span>},
    {path: 'edit', element: <Editor/>, action: async ({request}) => ({saved: (await request.json()).name})},
    {path: 'failure', element: <span>unreachable</span>, errorElement: <ErrorPage/>,
      loader: () => {throw new Response('missing', {status: 404, statusText: 'Not Found'});}},
    {path: 'slow', element: <span>stale</span>, loader: ({request}) => {
      slowSignal = request.signal;
      slowStarted();
      return new Promise(resolve => {release = resolve;});
    }}
  ]}], {initialEntries: ['/']});
  const view = mount(<RouterProvider router={router}/>);
  const byId = id => view.container.querySelector('#router-' + id);
  collect();
  await observer.change('/users/12?mode=detail', () => router.navigate('/users/12?mode=detail'));
  check('data router async loader and Request', byId('data').textContent === '12:detail' && loads === 1 && router.state.navigation.state === 'idle');
  await observer.change('/users/99', () => router.navigate('/redirect'));
  check('loader redirect', byId('data').textContent === '99:default' && router.state.location.pathname === '/users/99');
  await observer.change('/edit', () => router.navigate('/edit', {formMethod: 'post', formEncType: 'application/json', body: {name: 'saved'}}));
  check('action submission and action data', byId('action').textContent === 'saved' && router.state.actionData !== null);
  await observer.change('/failure', () => router.navigate('/failure'));
  check('route error boundary and Response', byId('error').textContent === '404:missing');
  const slow = router.navigate('/slow');
  await started;
  collect();
  await observer.change('/users/15', () => router.navigate('/users/15'));
  release('obsolete');
  await slow;
  check('superseded loader cancellation', slowSignal.aborted && byId('data').textContent === '15:default' && !view.container.textContent.includes('stale'));
  view.unmount();
  router.dispose();
  collect();
  check('router disposal', view.container.textContent === '');
}

async function main() {
  const matched = matchRoutes([{path: '/users/:id', id: 'user'}], '/users/27');
  check('route matching utilities', matched[0].params.id === '27' && matchPath('/users/:id', '/users/8').params.id === '8' && generatePath('/users/:id', {id: '11'}) === '/users/11');
  await declarative();
  await dataRouter();
}

runSuite('React Router', dependencies['react-router'], main);
