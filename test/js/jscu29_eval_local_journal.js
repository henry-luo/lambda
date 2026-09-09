// Direct eval-created vars remain in the caller-local journal after the
// temporary global bridge is removed. This crosses the former 512-entry cap.
function evaluate_many_locals() {
    let source = '';
    for (let i = 0; i < 513; i++) {
        source += 'var evalLocal' + i + ' = ' + i + ';';
    }
    source += 'evalLocal512;';

    console.log(eval(source));
    console.log(eval('evalLocal512'));
}

evaluate_many_locals();
