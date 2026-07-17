console.log('initial:', history.length, history.state === null);

var firstState = { page: 1, nested: { value: 'kept' } };
history.pushState(firstState, '', '#one');
firstState.nested.value = 'changed';
console.log('push:', history.length, history.state.page, history.state.nested.value, location.hash);

history.replaceState({ page: 2 }, '', '#two');
console.log('replace:', history.length, history.state.page, location.hash);

history.pushState({ page: 3 }, '', '#three');
console.log('second-push:', history.length, history.state.page, location.hash);

window.addEventListener('popstate', function (event) {
  console.log('popstate:', event.state.page, history.state.page, location.hash);
  location.hash = '#local';
});
window.addEventListener('hashchange', function (event) {
  console.log('hashchange:', event.oldURL.slice(event.oldURL.indexOf('#')), event.newURL.slice(event.newURL.indexOf('#')));
});

history.go(-1);
