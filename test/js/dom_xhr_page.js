console.log(typeof XMLHttpRequest);
var xhr = new XMLHttpRequest();
xhr.open('GET', 'dom_xhr_page_payload.json');
console.log(typeof xhr.overrideMimeType);
console.log(typeof xhr.upload, typeof xhr.upload.addEventListener);
xhr.overrideMimeType('application/json');
xhr.onload = function () {
  console.log(xhr.status);
  console.log(xhr.statusText);
  console.log(xhr.responseText.indexOf('xhr-page-ok') >= 0);
  console.log(xhr.readyState);
};
xhr.onerror = function () { console.log('XHR_ERROR'); };
xhr.send();
console.log('after:' + xhr.status);
console.log('XHR_PAGE_DONE');
