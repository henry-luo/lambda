class TestCustomElement extends HTMLElement {}

console.log(typeof customElements);
console.log(customElements instanceof CustomElementRegistry);
console.log(customElements.get("test-custom-element") === undefined);

customElements.whenDefined("test-custom-element").then(function(value) {
  console.log(value === TestCustomElement);
});
customElements.define("test-custom-element", TestCustomElement);

console.log(customElements.get("test-custom-element") === TestCustomElement);
const constructed = new TestCustomElement();
const created = document.createElement("test-custom-element");
console.log(constructed instanceof TestCustomElement);
console.log(constructed instanceof HTMLElement);
console.log(constructed.localName);
console.log(created instanceof TestCustomElement);
try {
  customElements.define("test-custom-element", TestCustomElement);
} catch (error) {
  console.log(error.name);
}
try {
  customElements.define("invalid", TestCustomElement);
} catch (error) {
  console.log(error.name);
}
