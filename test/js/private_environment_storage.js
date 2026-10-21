let fields = '';
for (let i = 0; i < 2048; i++) fields += '#field' + i + ';';
eval('class LargePrivate {' + fields + '} globalThis.LargePrivate = LargePrivate;');
if (typeof LargePrivate !== 'function') throw new Error('large private environment');

const mapDescriptor = Object.getOwnPropertyDescriptor(globalThis, 'Map');
Object.defineProperty(globalThis, 'Map', {
    get() { throw new Error('private bindings observed the Map global'); },
    configurable: true
});
class OuterPrivate {
    #value = 40;
    get #pair() { return this.#value; }
    set #pair(value) { this.#value = value; }
    create() {
        const outer = this;
        return class InnerPrivate {
            #value = 2;
            read() { return eval('outer.#pair + this.#value'); }
            write(value) { outer.#pair = value; }
        };
    }
}
Object.defineProperty(globalThis, 'Map', mapDescriptor);
const InnerPrivate = new OuterPrivate().create();
const instance = new InnerPrivate();
gc();
if (instance.read() !== 42) throw new Error('private lexical lookup after GC');
instance.write(41);
gc();
if (instance.read() !== 43) throw new Error('private accessor identity after GC');
console.log('private environment: PASS');
