import React, { useEffect, useRef, useState } from 'react';
import { InspectorClient } from '../inspector/client.mjs';
import { MarkElement, encodeMark } from '../inspector/mark.mjs';
import '../styles/inspector.css';
import LambdaScratch from './LambdaScratch';

function NodeRow({ row, branches, selected, select, expand, more, previous, depth = 0 }) {
  const branch = branches[row.node];
  const element = row.value instanceof MarkElement;
  const attrs = element ? row.value.attrs : {};
  const label = element ? `<${row.name}${attrs.id ? ` id="${attrs.id}"` : ''}${attrs.class ? ` class="${attrs.class}"` : ''}>`
    : String(row.value ?? row.name).replace(/\s+/g, ' ').slice(0, 100);
  return <>
    <div className={`inspector-node ${selected === row.node ? 'selected' : ''}`} style={{ paddingLeft: 10 + depth * 16 }}>
      <button className="inspector-expand" disabled={!row.count} onClick={() => expand(row.node)} aria-label={`Expand ${row.name}`}>
        {row.count ? branch ? '▾' : '▸' : ' '}
      </button>
      <button className="inspector-node-label" onClick={() => select(row)} title={label}>{label || 'whitespace'}</button>
    </div>
    {branch?.offset > 0 && <button className="btn inspector-more" onClick={() => previous(row.node)}>Previous children</button>}
    {branch?.children.filter(child => typeof child.value !== 'string' || child.value.trim()).map(child => <NodeRow key={child.node} row={child} branches={branches} selected={selected}
      select={select} expand={expand} more={more} previous={previous} depth={depth + 1} />)}
    {branch && branch.count > (branch.offset || 0) + branch.children.length && <button className="btn inspector-more" onClick={() => more(row.node)}>Load more children ({(branch.offset || 0) + branch.children.length}/{branch.count})</button>}
  </>;
}

export default function LiveInspector({ defaultPath }) {
  const [document, setDocument] = useState(defaultPath || 'test/devtools/document.html');
  const [nativeWindow, setNativeWindow] = useState(false);
  const [status, setStatus] = useState('Disconnected');
  const [connected, setConnected] = useState(false);
  const [root, setRoot] = useState(null);
  const [branches, setBranches] = useState({});
  const [selection, setSelection] = useState(null);
  const [style, setStyle] = useState(null);
  const [revision, setRevision] = useState(0);
  const [snapshot, setSnapshot] = useState(null);
  const [source, setSource] = useState(null);
  const [tab, setTab] = useState('styles');
  const [property, setProperty] = useState('width');
  const [value, setValue] = useState('');
  const [priority, setPriority] = useState(false);
  const [undo, setUndo] = useState(null);
  const [picking, setPicking] = useState(false);
  const [filter, setFilter] = useState('');
  const [logs, setLogs] = useState([]);
  const [consoleTab, setConsoleTab] = useState('output');
  const client = useRef(null);
  const current = useRef({ branches, selection });
  current.current = { branches, selection };
  const refreshTimer = useRef(null);
  const imageUrl = useRef(null);
  const log = text => setLogs(previous => [...previous, text].slice(-100));

  function clearImage() {
    if (imageUrl.current) URL.revokeObjectURL(imageUrl.current);
    imageUrl.current = null;
  }
  function stop() {
    client.current?.disconnect();
    client.current = null;
    window.electronAPI.devtoolsStop();
    setConnected(false);
    setStatus('Disconnected');
    setUndo(null);
  }
  useEffect(() => {
    const unsubscribe = window.electronAPI.onDevtoolsOutput(log);
    return () => {
      unsubscribe();
      clearTimeout(refreshTimer.current);
      client.current?.disconnect();
      window.electronAPI.devtoolsStop();
      clearImage();
    };
  }, []);

  async function capture(active) {
    const reply = await active.request('screenshot');
    if (client.current !== active) return;
    clearImage();
    imageUrl.current = URL.createObjectURL(new Blob([reply.value.pixels.bytes], { type: reply.value.mime }));
    setSnapshot({ url: imageUrl.current, width: Number(reply.value.width), height: Number(reply.value.height), revision: reply.rev });
  }
  async function select(row, active = client.current) {
    if (!active) return;
    setSelection(row);
    setUndo(null);
    setStyle(null);
    const path = (await active.request('path', { node: row.node })).value;
    const revealed = {};
    for (let i = 0; i + 1 < path.length; i++) {
      const node = path[i].node.node, index = path[i + 1].offset;
      const existing = current.current.branches[node];
      if (existing && index >= (existing.offset || 0) && index < (existing.offset || 0) + existing.children.length) continue;
      const offset = Math.floor(index / 100) * 100;
      revealed[node] = { ...(await active.request('tree', { node, offset })).value, offset };
    }
    if (client.current !== active) return;
    setBranches(previous => ({ ...previous, ...revealed }));
    if (!(row.value instanceof MarkElement)) return;
    const reply = await active.request('style', { node: row.node });
    if (client.current !== active) return;
    setStyle(reply.value);
    setRevision(reply.rev);
  }
  async function refresh(active = client.current) {
    if (!active || active.closed) return;
    const reply = await active.request('tree');
    if (client.current !== active) return;
    const next = { [reply.value.node.node]: reply.value };
    for (const node of Object.keys(current.current.branches)) {
      if (Number(node) === reply.value.node.node) continue;
      try {
        const count = current.current.branches[node].children.length;
        const start = current.current.branches[node].offset || 0;
        let offset = start;
        while (offset - start < count) {
          const page = (await active.request('tree', { node: Number(node), offset, limit: Math.min(500, count - offset + start) })).value;
          next[node] = offset > start ? { ...page, offset: start, children: [...next[node].children, ...page.children] } : { ...page, offset: start };
          offset += page.children.length;
          if (!page.children.length || offset >= page.count) break;
        }
      }
      catch (error) { if (error.code !== 'stale') throw error; }
    }
    setRoot(reply.value.node);
    setBranches(next);
    setRevision(reply.rev);
    if (current.current.selection) {
      try {
        const row = (await active.request('node', { node: current.current.selection.node })).value.node;
        setSelection(row);
        if (row.value instanceof MarkElement) setStyle((await active.request('style', { node: row.node })).value);
      } catch (error) { if (error.code === 'stale') { setSelection(null); setStyle(null); setUndo(null); } else throw error; }
    }
    await capture(active);
  }
  const attempt = action => Promise.resolve().then(action).catch(error => { log(error.message); setStatus(error.message); });

  async function launch() {
    stop();
    clearImage();
    setSnapshot(null); setRoot(null); setBranches({}); setSelection(null); setStyle(null); setSource(null); setLogs([]);
    setStatus('Connecting…');
    const bridge = window.electronAPI;
    const active = new InspectorClient({
      send: text => bridge.devtoolsSend(text),
      onMessage: callback => bridge.onDevtoolsMessage(callback),
      onClose: callback => bridge.onDevtoolsClose(reason => {
        callback(reason);
        setConnected(false); setStatus(reason); log(reason);
      })
    });
    client.current = active;
    active.onEvent(message => {
      if (message.event.name === 'changed') {
        setRevision(message.rev);
        clearTimeout(refreshTimer.current);
        refreshTimer.current = setTimeout(() => attempt(() => refresh(active)), 80);
      } else if (message.event.name === 'selected' && message.value) attempt(() => select(message.value, active));
      else if (message.event.name === 'log') log(encodeMark(message));
    });
    await bridge.devtoolsStart(document, !nativeWindow);
    const hello = await active.request('hello', { version: 1 });
    setConnected(true);
    setStatus(`Connected · protocol ${hello.value.version}`);
    log(`Supported operations: ${hello.value.ops.map(op => op.name).join(', ')}`);
    await refresh(active);
  }

  async function expand(node) {
    if (branches[node]) {
      setBranches(previous => { const next = { ...previous }; delete next[node]; return next; });
    } else {
      const reply = await client.current.request('tree', { node });
      setBranches(previous => ({ ...previous, [node]: reply.value }));
    }
  }
  async function more(node) {
    const branch = branches[node];
    const reply = await client.current.request('tree', { node, offset: (branch.offset || 0) + branch.children.length });
    setBranches(previous => ({ ...previous, [node]: { ...reply.value, offset: branch.offset || 0, children: [...previous[node].children, ...reply.value.children] } }));
  }
  async function previous(node) {
    const offset = Math.max(0, branches[node].offset - 100);
    const reply = await client.current.request('tree', { node, offset });
    setBranches(current => ({ ...current, [node]: { ...reply.value, offset } }));
  }
  async function edit(restore = false) {
    if (!selection) return;
    const fields = restore ? undo : { name: property, value, priority: priority ? 'important' : '' };
    const reply = await client.current.request('set_style', { ...fields, node: selection.node, rev: revision });
    setRevision(reply.rev);
    setUndo(restore ? null : reply.value);
    log(`${restore ? 'Restored' : 'Changed'} ${fields.name}${fields.priority ? ' !important' : ''}`);
    await refresh();
  }
  async function pick({ x, y, rect }) {
    if (!picking || !snapshot) return;
    await client.current.request('pick', {
      x: (x - rect.left) * snapshot.width / rect.width,
      y: (y - rect.top) * snapshot.height / rect.height
    });
    setPicking(false);
  }
  async function showSource() {
    setTab('source');
    if (!source) setSource((await client.current.request('source')).value);
  }
  const box = style?.box || selection?.box;
  const entries = Object.entries(style?.computed || {}).filter(([name]) => name.includes(filter));

  return <section className="live-inspector">
    <div className="inspector-launch">
      <label htmlFor="inspector-document">Document</label>
      <input id="inspector-document" value={document} onChange={event => setDocument(event.target.value)} placeholder="Path to an HTML or Lambda document" />
      <label className="inspector-native"><input type="checkbox" checked={nativeWindow} onChange={event => setNativeWindow(event.target.checked)} /> Native window</label>
      <button className="btn btn-primary" onClick={() => attempt(launch)}>Launch</button>
      <button className="btn" disabled={!connected} onClick={stop}>Stop</button>
      <button className="btn" disabled={!connected} onClick={() => attempt(() => refresh())}>Refresh</button>
    </div>
    <div className="inspector-status"><span className={connected ? 'inspector-connected' : ''}>{status}</span><span>Revision {revision}</span></div>
    <div className="inspector-panes">
      <div className="inspector-tree"><div className="inspector-panel-title">Elements</div>
        {root ? <NodeRow row={root} branches={branches} selected={selection?.node} select={row => attempt(() => select(row))} expand={node => attempt(() => expand(node))} more={node => attempt(() => more(node))} previous={node => attempt(() => previous(node))} />
          : <p className="inspector-empty">Launch a document to inspect its live DOM.</p>}
      </div>
      <div className="inspector-center">
        <div className="inspector-panel-title">Radiant preview
          <button className={`btn ${picking ? 'active' : ''}`} disabled={!connected} onClick={() => setPicking(!picking)}>{picking ? 'Click an element…' : 'Pick element'}</button>
        </div>
        <div className="inspector-preview">
          {snapshot ? <div className="inspector-image" style={{ maxWidth: snapshot.width }}>
            <img src={snapshot.url} data-revision={snapshot.revision} alt="Live Radiant rendering" onClick={event => {
              // React clears currentTarget after dispatch; capture geometry before deferring.
              const point = { x: event.clientX, y: event.clientY, rect: event.currentTarget.getBoundingClientRect() };
              attempt(() => pick(point));
            }} style={{ cursor: picking ? 'crosshair' : 'default' }} />
            {box && selection && <div className="inspector-highlight" style={{ left: `${100 * Number(box.x) / snapshot.width}%`, top: `${100 * Number(box.y) / snapshot.height}%`, width: `${100 * Number(box.width) / snapshot.width}%`, height: `${100 * Number(box.height) / snapshot.height}%` }} />}
          </div> : <p className="inspector-empty">Rendered pixels appear here.</p>}
        </div>
      </div>
      <div className="inspector-details">
        <div className="inspector-tabs"><button className={tab === 'styles' ? 'active' : ''} onClick={() => setTab('styles')}>Styles</button>
          <button disabled={!connected} className={tab === 'source' ? 'active' : ''} onClick={() => attempt(showSource)}>Source</button></div>
        {tab === 'source' ? <><p className="inspector-note">{source?.path} · file snapshot at attach</p><pre className="inspector-source">{source?.text}</pre></>
          : selection ? <>
            <div className="inspector-selection">{selection.name} <span>node {selection.node}</span></div>
            {box && <div className="inspector-box">{Number(box.width).toFixed(2)} × {Number(box.height).toFixed(2)}<small>x {Number(box.x).toFixed(2)} · y {Number(box.y).toFixed(2)} CSS px</small></div>}
            {style && <><div className="inspector-edit"><input aria-label="CSS property" value={property} onChange={event => setProperty(event.target.value)} />
              <input aria-label="CSS value" value={value} onChange={event => setValue(event.target.value)} placeholder="Value (empty removes)" />
              <label><input type="checkbox" checked={priority} onChange={event => setPriority(event.target.checked)} /> !important</label>
              <button className="btn btn-primary" disabled={!connected} onClick={() => attempt(() => edit())}>Apply</button>
              <button className="btn" disabled={!undo || !connected} onClick={() => attempt(() => edit(true))}>Undo</button></div>
              <div className="inspector-authored"><strong>Inline style</strong><code>{style.inline || '(none)'}</code></div>
              <input className="inspector-filter" aria-label="Filter computed styles" placeholder="Filter computed properties" value={filter} onChange={event => setFilter(event.target.value)} />
              <dl className="inspector-computed">{entries.map(([name, computed]) => <React.Fragment key={name}><dt>{name}</dt><dd>{computed}</dd></React.Fragment>)}</dl>
            </>}
          </> : <p className="inspector-empty">Select an element to inspect its styles and box.</p>}
      </div>
    </div>
    <div className={`inspector-console ${consoleTab === 'scratch' ? 'scratch-open' : ''}`}>
      <div className="inspector-tabs"><button className={consoleTab === 'output' ? 'active' : ''} onClick={() => setConsoleTab('output')}>Process output</button>
        <button className={consoleTab === 'scratch' ? 'active' : ''} onClick={() => setConsoleTab('scratch')}>Lambda scratch</button></div>
      <div className="inspector-log" hidden={consoleTab !== 'output'}><pre>{logs.join('\n')}</pre></div>
      <div hidden={consoleTab !== 'scratch'} className="scratch-container"><LambdaScratch /></div>
    </div>
  </section>;
}
