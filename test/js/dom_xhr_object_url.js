var xhr = new XMLHttpRequest();
var method = { toString: function () { return 'GET'; } };
var requestUrl = {
  toString: function () { return 'dom_xhr_page_payload.json'; }
};

xhr.open(method, requestUrl);
xhr.onload = function () {
  console.log(xhr.status);
  console.log(xhr.responseText.indexOf('xhr-page-ok') >= 0);
};
xhr.onerror = function () { console.log('XHR_ERROR'); };
xhr.send();
console.log('XHR_OBJECT_URL_DONE');
