var host = document.getElementById('host');
var delivery = 0;
var observer = new MutationObserver(function (records) {
  delivery++;
  if (delivery === 1) {
    console.log('replace:' + records.length);
    console.log(records[0].removedNodes.length + ':' + records[0].addedNodes.length);
    console.log(records[1].removedNodes.length + ':' + records[1].addedNodes.length);
    document.getElementById('dynamic').remove();
    return;
  }
  console.log('remove:' + records.length);
  console.log(records[0].removedNodes[0].id);
  observer.disconnect();
});

observer.observe(host, {childList: true});
host.innerHTML = '<span id="dynamic">new</span>';
