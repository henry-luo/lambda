import {Application, Controller} from '@hotwired/stimulus';
import {assertions, collect, fixture, until, runSuite} from './support.js';

const check = assertions('Stimulus');
runSuite('Stimulus', '3.2.2', async () => {
  const root = fixture();
  root.innerHTML = '<section data-controller="counter" data-counter-count-value="2" data-counter-enabled-value="true" data-counter-label-value="ready" data-counter-items-value="[1,2]" data-counter-options-value=\'{"step":3}\' data-counter-active-class="active"><output data-counter-target="output"></output><button data-action="click->counter#increment:prevent" data-counter-step-param="3">Add</button><div data-controller="counter" data-counter-count-value="10"><output data-counter-target="output"></output></div></section>';
  const changes = [];
  let connections = 0, disconnections = 0, addedTargets = 0, removedTargets = 0;
  class Counter extends Controller {
    static targets = ['output'];
    static values = {count: Number, enabled: Boolean, label: String, items: Array, options: Object};
    static classes = ['active'];
    connect() { connections++; this.render(); this.element.dataset.connected = 'yes'; }
    disconnect() { disconnections++; }
    outputTargetConnected() { addedTargets++; }
    outputTargetDisconnected() { removedTargets++; }
    countValueChanged(value, previous) { changes.push([value, previous]); this.render(); }
    render() { if (this.hasOutputTarget) this.outputTarget.textContent = String(this.countValue); }
    increment(event) {
      this.countValue += event.params.step;
      this.element.classList.add(this.activeClass);
    }
  }
  const application = Application.start(root);
  application.register('counter', Counter);
  await until(() => connections === 2, 'controller connection');
  const outer = root.firstElementChild;
  const inner = outer.querySelector('[data-controller]');
  const controller = application.getControllerForElementAndIdentifier(outer, 'counter');
  check('controllers and nested scopes', controller instanceof Counter && controller.outputTargets.length === 1 && inner.textContent === '10');
  check('typed values', controller.countValue === 2 && controller.enabledValue && controller.labelValue === 'ready' && controller.itemsValue.join(',') === '1,2' && controller.optionsValue.step === 3);
  check('initial value callbacks', changes.some(([value, old]) => value === 2 && old === 0));
  const event = new MouseEvent('click', {bubbles: true, cancelable: true});
  outer.querySelector('button').dispatchEvent(event);
  await until(() => controller.outputTarget.textContent === '5', 'action value change');
  check('actions parameters and prevent', event.defaultPrevented && controller.countValue === 5);
  check('classes and target isolation', outer.classList.contains('active') && inner.textContent === '10');
  outer.setAttribute('data-counter-count-value', '7');
  await until(() => controller.outputTarget.textContent === '7', 'attribute value observer');
  check('external value changes', changes.some(([value, old]) => value === 7 && old === 5));
  const extra = document.createElement('output');
  extra.setAttribute('data-counter-target', 'output');
  outer.appendChild(extra);
  await until(() => addedTargets === 3, 'target connection');
  check('dynamic target connection', controller.outputTargets.length === 2);
  extra.remove();
  await until(() => removedTargets === 1, 'target disconnection');
  check('dynamic target disconnection', controller.outputTargets.length === 1);
  inner.remove();
  await until(() => disconnections === 1, 'nested disconnect');
  outer.appendChild(inner);
  await until(() => connections === 3, 'nested reconnect');
  check('controller reconnection', application.getControllerForElementAndIdentifier(inner, 'counter').countValue === 10);
  collect();
  check('retained controller after gc', controller.countValue === 7 && controller.outputTarget.textContent === '7');
  application.stop();
  outer.querySelector('button').dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true}));
  check('application stops actions', controller.countValue === 7);
  root.remove();
});
