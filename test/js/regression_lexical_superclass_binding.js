function buildEmitter() {
    class r {
        constructor() {
            this.value = "wrong";
        }
    }

    function compileRule(r) {
        const o = r;
        return o;
    }

    class o {
        constructor(value) {
            this.value = value;
        }
    }

    class l extends o {
        constructor(value) {
            super(value);
        }
    }

    return new l("right").value;
}

console.log(buildEmitter());
