function invoke() {
    const offset = 2;
    function add(value) {
        return value + offset;
    }
    const api = {
        call(value) {
            return add(value);
        }
    };
    return api.call(5);
}

console.log(invoke());
