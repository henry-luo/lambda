fetch('/slow-fetch')
  .then(response => response.text())
  .then(text => fetch('/health').then(response => response.text())
    .then(health => console.log('pending fetch: ' + text + ' ' + health)))
  .catch(error => console.log('pending fetch failed: ' + error));
