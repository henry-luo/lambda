import React, { useEffect, useRef, useState } from 'react';
import { InspectorClient } from '../inspector/client.mjs';
import { encodeMark } from '../inspector/mark.mjs';

function Value({ value, request, ancestors = [] }) {
  const [page, setPage] = useState(null);
  const [error, setError] = useState('');
  const cyclic = value.ref && ancestors.includes(value.ref);
  useEffect(() => { setPage(null); setError(''); }, [value.ref]);
  async function expand(more = false) {
    try {
      const reply = await request('inspect', { ref: value.ref, offset: more ? page.next : 0 });
      setPage(previous => more && previous ? { ...reply.value, entries: [...(previous.entries || []), ...(reply.value.entries || [])],
        value: typeof previous.value === 'string' ? previous.value + reply.value.value : reply.value.value } : reply.value);
    } catch (failure) { setError(failure.message); }
  }
  const label = Object.hasOwn(value, 'value') ? encodeMark(value.value) : value.text ??
    `${value.tag ? `<${value.tag}> ` : ''}${value.type.name}${value.count !== undefined ? ` (${value.count})` : ''}${value.opaque ? ' · opaque' : ''}`;
  return <div className="scratch-value">
    {value.ref && !cyclic && <button className="scratch-expand" onClick={() => page ? setPage(null) : expand()} aria-label="Expand value">{page ? '▾' : '▸'}</button>}
    <code>{label}</code>{cyclic && <span className="inspector-note"> ↩ ref {value.ref}</span>}
    {error && <p className="scratch-error">{error}</p>}
    {page && <div className="scratch-properties">
      {page.entries?.map((entry, i) => <div key={`${entry.kind.name}:${encodeMark(entry.key)}:${i}`}>
        <span className="scratch-key">{encodeMark(entry.key)}: </span><Value value={entry.value} request={request} ancestors={[...ancestors, value.ref]} />
      </div>)}
      {Object.hasOwn(page, 'value') && <pre>{encodeMark(page.value)}</pre>}
      {page.next < page.count && <button className="btn" onClick={() => expand(true)}>Load more values</button>}
    </div>}
  </div>;
}

export default function LambdaScratch() {
  const [status, setStatus] = useState('Stopped');
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [source, setSource] = useState('');
  const [rows, setRows] = useState([]);
  const [bindings, setBindings] = useState(null);
  const [output, setOutput] = useState('');
  const client = useRef(null);
  const history = useRef([]);
  const cursor = useRef(0);
  const bridge = window.electronAPI.scratch;

  async function request(op, fields) {
    const active = client.current;
    if (!active) throw new Error('Start the Lambda scratch session first');
    const reply = await active.request(op, fields);
    if (client.current !== active) throw new Error('Scratch session changed');
    return reply;
  }
  function stop() {
    client.current?.disconnect(); client.current = null;
    bridge.stop();
    setConnected(false); setBusy(false); setBindings(null); setStatus('Stopped');
  }
  useEffect(() => {
    const off = bridge.onOutput(text => setOutput(previous => (previous + text).slice(-32768)));
    return () => { off(); client.current?.disconnect(); bridge.stop(); };
  }, []);

  async function start() {
    stop(); setRows([]); setOutput(''); setStatus('Starting…');
    setBusy(true);
    const active = new InspectorClient({ send: bridge.send, onMessage: bridge.onMessage,
      onClose: callback => bridge.onClose(reason => {
        callback(reason); setConnected(false); setBusy(false); setStatus(reason);
      }) });
    client.current = active;
    try {
      await bridge.start();
      if (client.current !== active) return;
      await active.request('hello', { version: 1 });
      if (client.current !== active) return;
      setConnected(true); setStatus('Lambda scratch · independent session');
    } finally { if (client.current === active) setBusy(false); }
  }
  async function submit() {
    if (!source.trim()) return;
    const submitted = source;
    const active = client.current;
    history.current.push(submitted); cursor.current = history.current.length;
    setBusy(true);
    try {
      const reply = await request('eval', { source: submitted });
      setRows(previous => [...previous, { source: submitted, ...reply.value }]);
      setSource(''); setBindings(null);
    } catch (error) {
      // A stopped/replaced session cannot publish a late result into its successor.
      if (client.current === active)
        setRows(previous => [...previous, { source: submitted, error: `${error.code || 'error'}: ${error.message}` }]);
    } finally { if (client.current === active) setBusy(false); }
  }
  async function clear() {
    // One owner-thread release also catches handles from an overlapping expansion.
    await request('release', { all: true });
    setRows([]); setBindings(null); setOutput('');
  }
  async function readBindings(more = false) {
    const offset = more ? bindings.entries.length : 0;
    const reply = await request('bindings', { offset });
    setBindings(previous => more ? { ...reply.value, entries: [...previous.entries, ...reply.value.entries] } : reply.value);
  }
  const attempt = fn => Promise.resolve().then(fn).catch(error => setStatus(error.message));
  function keyDown(event) {
    if ((event.metaKey || event.ctrlKey) && event.key === 'Enter' && connected && !busy) {
      event.preventDefault(); submit();
    } else if (event.altKey && ['ArrowUp', 'ArrowDown'].includes(event.key)) {
      event.preventDefault();
      cursor.current = Math.max(0, Math.min(history.current.length, cursor.current + (event.key === 'ArrowUp' ? -1 : 1)));
      setSource(history.current[cursor.current] || '');
    }
  }
  return <section className="lambda-scratch">
    <div className="scratch-toolbar"><span>{status}</span>
      <button className="btn" disabled={connected || busy} onClick={() => attempt(start)}>Start scratch</button>
      <button className="btn" disabled={!connected && !busy} onClick={stop}>Stop scratch</button>
      <button className="btn" disabled={!connected || busy} onClick={() => attempt(clear)}>Clear results</button>
      <button className="btn" disabled={!connected || busy} onClick={() => attempt(() => readBindings())}>Bindings</button>
      <button className="btn" disabled={!connected || busy} onClick={() => attempt(async () => {
        const reply = await request('gc'); setStatus(`Collected · ${reply.value.retained} inspector values retained`);
      })}>Collect</button>
    </div>
    <div className="scratch-transcript" aria-live="polite">
      <p className="scratch-hint">Lambda REPL in a separate process. It has its own bindings. Cmd/Ctrl+Enter runs an entry; Alt+↑/↓ recalls history.</p>
      {rows.map((row, i) => <div className="scratch-entry" key={i}><pre className="scratch-input">λ&gt; {row.source}</pre>
        {row.error ? <p className="scratch-error">{row.error}</p> : row.declarations ? <span className="scratch-hint">Binding accepted</span>
          : <Value value={row.result} request={request} />}</div>)}
      {bindings && <div className="scratch-bindings"><strong>Bindings ({bindings.count})</strong>
        {bindings.entries.map(row => <div key={row.name}><span className="scratch-key">{row.name}{row.imported ? ' (import)' : ''}: </span><Value value={row.value} request={request} /></div>)}
        {bindings.entries.length < bindings.count && <button className="btn" onClick={() => attempt(() => readBindings(true))}>More bindings</button>}
      </div>}
      {output && <details className="scratch-output"><summary>Scratch process diagnostics</summary><pre>{output}</pre></details>}
    </div>
    <div className="scratch-prompt"><textarea aria-label="Lambda source" value={source} onChange={event => setSource(event.target.value)} onKeyDown={keyDown} placeholder="let data = {items: [1, 2, 3]}" />
      <button className="btn btn-primary" disabled={!connected || busy || !source.trim()} onClick={submit}>{busy ? 'Running…' : 'Run Lambda'}</button></div>
  </section>;
}
