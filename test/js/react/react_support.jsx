import {createRoot} from 'react-dom/client';
import {flushSync} from 'react-dom';
import {fixture} from './support';

export function mount(element) {
  const container = fixture();
  const root = createRoot(container);
  flushSync(() => root.render(element));
  return {
    container,
    render(next) { flushSync(() => root.render(next)); },
    unmount() { flushSync(() => root.unmount()); container.remove(); }
  };
}
