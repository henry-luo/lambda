writeOrder.push('external:' + document.currentScript.id);
document.write('<script id="nested">writeOrder.push("nested:" + document.currentScript.id);window.writtenValue = 42;<\/script>');
writeOrder.push('resume:' + document.currentScript.id);
