(function () {
    function o(value) {
        return value[0];
    }

    function identity(value) {
        return value;
    }

    function consume(first, second) {
        var eventValue = (o = arguments.length > 2 ? arguments[2] : {}).eventValue,
            o = identity(o);
        return function () {
            return [eventValue, o, first, second];
        };
    }

    consume(1, 2, { eventValue: 3 })();
    console.log('var-hoist-later-declarator:', o([7]));
})();
