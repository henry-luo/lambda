var interval = setInterval(() => {}, 50);
console.log('timer-coercion:', typeof interval[Symbol.toPrimitive], Number(interval) > 0, interval > -1);
clearInterval(interval);
console.log('timer-cleared:', interval._destroyed);

class TimerOwner {
    constructor() {
        this.scrolling = -1;
    }

    start() {
        this.scrolling = setInterval(() => {}, 50);
    }

    stop() {
        if (this.scrolling > -1) {
            clearInterval(this.scrolling);
            this.scrolling = -1;
            return true;
        }
        return false;
    }
}

var owner = new TimerOwner();
owner.start();
var memberInterval = owner.scrolling;
console.log('timer-member-coercion:', typeof memberInterval[Symbol.toPrimitive], Number(memberInterval) > 0, memberInterval > -1);
console.log('timer-member-cleared:', owner.stop(), memberInterval._destroyed);
