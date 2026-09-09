let source = 'console.log(17);';
for (let i = 0; i < 17; i++) {
    source = 'eval(' + JSON.stringify(source) + ');';
}

eval(source);
