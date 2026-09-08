setInterval("document.body.setAttribute('data-interval', 'ran');", 4000);
setTimeout(
    "document.body.setAttribute('data-timeout', this === window ? 'window' : 'other');",
    0);
setTimeout(function() {
    console.log(document.body.getAttribute('data-timeout'));
}, 5);
