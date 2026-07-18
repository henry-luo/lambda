var Named = class n {};
var Reader = class {
    read() {
        let { view: n } = { view: 'local' };
        return n;
    }
};

console.log(new Reader().read());

function minified(options) {
    let{dispatch:i}=options;
    return i;
}

console.log(minified({ dispatch: 'kept' }));
