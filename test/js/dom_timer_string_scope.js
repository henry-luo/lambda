// @document dom_timer_string_handler.html
let timerShared = 40;
let timerOrder = [];
function scheduleTimerString() {
    let timerShared = -1;
    setTimeout({toString() {
        timerOrder.push('source');
        gc();
        return "timerShared++;var timerPublished=timerShared;let timerLexical=timerShared+1;" +
            "globalThis.timerReceiver=this===window;";
    }}, {valueOf() {
        timerOrder.push('delay');
        gc();
        return 0;
    }});
    return timerShared;
}
console.log('local:', scheduleTimerString());
setTimeout(function() {
    gc();
    console.log('order:', timerOrder.join(','));
    console.log('global:', timerShared, timerPublished, timerLexical, timerReceiver);
}, 5);
