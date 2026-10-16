class FrozenElement extends HTMLElement {
  answer = 42;
}
const namesBefore = Reflect.ownKeys(FrozenElement);
Object.freeze(FrozenElement);
Object.freeze(FrozenElement.prototype);
customElements.whenDefined("frozen-element").then(value => console.log(value === FrozenElement));
customElements.define("frozen-element", FrozenElement);
gc();
const element = new FrozenElement();
console.log(element.localName);
console.log(element.answer);
console.log(element instanceof FrozenElement && element instanceof HTMLElement);
console.log(document.createElement("frozen-element") instanceof FrozenElement);
console.log(Reflect.ownKeys(FrozenElement).length === namesBefore.length);
try {
  customElements.define("second-element", FrozenElement);
} catch (error) {
  console.log(error.name);
}
try {
  new HTMLElement();
} catch (error) {
  console.log(error.name);
}
