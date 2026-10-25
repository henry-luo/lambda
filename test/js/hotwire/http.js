import * as Turbo from '@hotwired/turbo';
import {Application, Controller} from '@hotwired/stimulus';
import {assertions, fixture, until, eventOnce, runSuite} from './support.js';

const check = assertions('Hotwire HTTP');
runSuite('Hotwire HTTP', '8.0.23', async () => {
  const response = await fetch('/health');
  check('lambda server responds', response.ok && await response.text() === 'lambda-hotwire');
  const application = Application.start();
  class Remote extends Controller {
    connect() { this.element.dataset.connected = 'yes'; }
    disconnect() { this.element.dataset.disconnected = 'yes'; }
  }
  application.register('remote', Remote);
  const root = fixture();
  const frame = document.createElement('turbo-frame');
  frame.id = 'message';
  frame.src = '/frame';
  const frameLoaded = eventOnce(frame, 'turbo:frame-load');
  root.appendChild(frame);
  await frameLoaded;
  await until(() => frame.querySelector('#remote')?.dataset.connected === 'yes', 'remote Stimulus connection');
  check('frame fetch and matching extraction', frame.textContent === 'Frame oneNext frame' && !document.querySelector('#outside-frame'));
  check('stimulus connects fetched html', application.getControllerForElementAndIdentifier(frame.querySelector('#remote'), 'remote') instanceof Remote);
  check('frame completes loading', frame.complete && !frame.hasAttribute('busy'));
  const nextLoaded = eventOnce(frame, 'turbo:frame-load');
  frame.querySelector('#frame-next').click();
  await nextLoaded;
  check('frame link navigation', frame.querySelector('#remote').textContent === 'Frame two');

  function form(action, fields) {
    const element = document.createElement('form');
    element.action = action;
    element.method = 'post';
    element.innerHTML = fields + '<button type="submit" name="commit" value="Save">Save</button>';
    document.body.appendChild(element);
    return element;
  }
  const notifications = document.createElement('div');
  notifications.id = 'notifications';
  document.body.appendChild(notifications);
  const streaming = form('/stream', '<input name="title" value="Stream">');
  const streamed = eventOnce(streaming, 'turbo:submit-end');
  streaming.requestSubmit(streaming.querySelector('button'));
  const streamResult = await streamed;
  await until(() => notifications.querySelector('#notification')?.dataset.connected === 'yes', 'stream response');
  check('form receives turbo stream response', streamResult.detail.success && notifications.textContent === 'Created over HTTP');
  check('stimulus connects streamed html', application.getControllerForElementAndIdentifier(notifications.firstElementChild, 'remote') instanceof Remote);
  check('form submitter restored', !streaming.querySelector('button').disabled);

  const oldBody = document.body;
  const oldHistoryLength = history.length;
  const link = document.createElement('a');
  link.href = '/page';
  root.appendChild(link);
  const visited = eventOnce(document, 'turbo:load');
  link.click();
  await visited;
  await until(() => document.querySelector('#page')?.dataset.connected === 'yes', 'page controller');
  check('drive replaces body and title', document.body !== oldBody && !oldBody.isConnected && document.title === 'Next page');
  check('drive updates url and history', location.pathname === '/page' && history.length === oldHistoryLength + 1);
  check('stimulus survives drive visit', application.getControllerForElementAndIdentifier(document.querySelector('#page'), 'remote') instanceof Remote);

  const saving = form('/save', '<input name="title" value="Hello Hotwire">');
  const saved = eventOnce(document, 'turbo:load');
  saving.requestSubmit(saving.querySelector('button'));
  await saved;
  check('post follows 303 redirect', location.pathname === '/saved' && document.body.textContent === 'Saved Hello Hotwire' && !!document.querySelector('#saved'));

  const invalid = form('/invalid', '<input name="title" value="">');
  const rejected = eventOnce(invalid, 'turbo:submit-end');
  invalid.requestSubmit(invalid.querySelector('button'));
  const rejectedResult = await rejected;
  await until(() => !!document.querySelector('#validation-error'), 'validation response');
  check('422 renders validation errors', !rejectedResult.detail.success && document.querySelector('#validation-error').textContent === 'Title is required');
  check('validation preserves visit url', location.pathname === '/saved');

  const audit = await (await fetch('/audit')).text();
  check('server observed frame request headers', /GET \/frame\naccept=[^\n]*\nframe=message\n/.test(audit) && /GET \/frame-next\naccept=[^\n]*\nframe=message\n/.test(audit));
  check('server observed stream accept header', /POST \/stream\naccept=text\/vnd\.turbo-stream\.html, text\/html, application\/xhtml\+xml\n/.test(audit));
  check('server parsed submitted form fields', audit.includes('POST /save\n') && audit.includes('body=title=Hello+Hotwire&commit=Save\n') && audit.includes('type=application/x-www-form-urlencoded;charset=UTF-8\n'));
  check('server observed redirect destination', audit.includes('GET /saved\n'));
  application.stop();
  Turbo.session.stop();
});
