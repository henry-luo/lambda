import * as Turbo from '@hotwired/turbo';
import {assertions, collect, fixture, until, runSuite} from './support.js';

const check = assertions('Turbo Streams');
runSuite('Turbo Streams', '8.0.23', async () => {
  const root = fixture();
  root.innerHTML = '<div id="messages"><p id="first">one</p></div><div id="status">old</div><p class="obsolete">x</p><p class="obsolete">y</p>';
  async function stream(action, target, html, predicate, attributes = '') {
    Turbo.renderStreamMessage('<turbo-stream action="' + action + '" target="' + target + '" ' + attributes + '><template>' + html + '</template></turbo-stream>');
    await until(predicate, action + ' stream');
  }
  const messages = root.querySelector('#messages');
  await stream('append', 'messages', '<p id="second">two</p>', () => !!root.querySelector('#second'));
  check('append', messages.textContent === 'onetwo');
  await stream('prepend', 'messages', '<p id="zero">zero</p>', () => !!root.querySelector('#zero'));
  check('prepend', messages.firstElementChild.id === 'zero');
  await stream('before', 'second', '<p id="before">before</p>', () => !!root.querySelector('#before'));
  check('before', root.querySelector('#second').previousElementSibling.id === 'before');
  await stream('after', 'second', '<p id="after">after</p>', () => !!root.querySelector('#after'));
  check('after', root.querySelector('#second').nextElementSibling.id === 'after');
  await stream('replace', 'first', '<p id="replacement">new</p>', () => !!root.querySelector('#replacement'));
  check('replace', !root.querySelector('#first') && root.querySelector('#replacement').textContent === 'new');
  const status = root.querySelector('#status');
  await stream('update', 'status', '<b>updated</b>', () => status.textContent === 'updated');
  check('update retains target', status === root.querySelector('#status') && status.firstElementChild.tagName === 'B');
  await stream('append', 'messages', '<p id="second">replacement two</p>', () => root.querySelector('#second').textContent === 'replacement two');
  check('duplicate id replaces child', messages.querySelectorAll('#second').length === 1);
  await stream('remove', 'zero', '', () => !root.querySelector('#zero'));
  check('remove', !root.querySelector('#zero'));
  Turbo.renderStreamMessage('<turbo-stream action="remove" targets=".obsolete"></turbo-stream>');
  await until(() => !root.querySelector('.obsolete'), 'multiple targets');
  check('selector targets', !root.querySelector('.obsolete'));
  let intercepted = 0;
  const intercept = event => {
    intercepted++;
    const render = event.detail.render;
    event.detail.render = async element => { await render(element); status.dataset.rendered = 'yes'; };
  };
  document.addEventListener('turbo:before-stream-render', intercept);
  await stream('update', 'status', 'hooked', () => status.dataset.rendered === 'yes');
  document.removeEventListener('turbo:before-stream-render', intercept);
  check('before render hook', intercepted === 1 && status.textContent === 'hooked');
  await until(() => document.querySelectorAll('turbo-stream').length === 0, 'stream cleanup');
  check('stream elements removed', document.querySelectorAll('turbo-stream').length === 0);
  collect();
  check('stream results survive gc', root.querySelector('#second').textContent === 'replacement two');
  root.remove();
  Turbo.session.stop();
});
