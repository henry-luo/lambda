function iterator_or_fallback(value) {
    var iterator = typeof Symbol !== 'undefined' && Symbol.iterator && value[Symbol.iterator];
    return iterator ? iterator.call(value) : {
        next: function() { return { done: true }; }
    };
}

const element = document.createElement('div');
console.log(iterator_or_fallback(element).next().done);

const children = document.body.childNodes;
console.log(typeof children[Symbol.iterator]);
console.log(iterator_or_fallback(children).next().done);
