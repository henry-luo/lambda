const order = [];

function decorate(value) {
    order.push(1);
    return value;
}

@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
@decorate
class Decorated {}

console.log(order.length, order[0], order[16], typeof Decorated);
