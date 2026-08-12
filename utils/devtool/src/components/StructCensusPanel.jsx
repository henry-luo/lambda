import React, { useState, useEffect, useMemo, useCallback } from 'react';

const NUMERIC = new Set(['size', 'align', 'members', 'line']);

// minimal RFC4180 reader: python's csv.writer quotes any field containing a
// comma or quote, so the viewer has to understand quoting to stay in sync
function parseCsv(text) {
  const rows = [];
  let field = '';
  let row = [];
  let quoted = false;

  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (quoted) {
      if (ch === '"') {
        if (text[i + 1] === '"') { field += '"'; i++; }
        else quoted = false;
      } else field += ch;
    } else if (ch === '"') {
      quoted = true;
    } else if (ch === ',') {
      row.push(field); field = '';
    } else if (ch === '\n') {
      row.push(field); rows.push(row); row = []; field = '';
    } else if (ch !== '\r') {
      field += ch;
    }
  }
  if (field.length || row.length) { row.push(field); rows.push(row); }
  if (!rows.length) return { columns: [], records: [] };

  const columns = rows[0];
  const records = rows.slice(1).filter(r => r.length === columns.length).map(r => {
    const rec = {};
    columns.forEach((c, idx) => {
      const raw = r[idx];
      rec[c] = NUMERIC.has(c) ? (raw === '' ? null : Number(raw)) : raw;
    });
    return rec;
  });
  return { columns, records };
}

function StructCensusPanel() {
  const [columns, setColumns] = useState([]);
  const [records, setRecords] = useState([]);
  const [generated, setGenerated] = useState(null);
  const [missing, setMissing] = useState(false);
  const [loading, setLoading] = useState(true);
  const [regenerating, setRegenerating] = useState(false);
  const [error, setError] = useState(null);

  const [query, setQuery] = useState('');
  const [moduleFilter, setModuleFilter] = useState('');
  const [kindFilter, setKindFilter] = useState('');
  const [minSize, setMinSize] = useState(0);
  const [sortCol, setSortCol] = useState('size');
  const [sortDir, setSortDir] = useState(-1);

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const res = await window.electronAPI.loadStructCensus();
      if (res.missing || !res.csv) {
        setMissing(true);
        setRecords([]);
      } else {
        const { columns: cols, records: recs } = parseCsv(res.csv);
        setColumns(cols);
        setRecords(recs);
        setGenerated(res.generated);
        setMissing(false);
      }
    } catch (e) {
      setError(e.message);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { load(); }, [load]);

  const handleRegen = async (full = false) => {
    if (regenerating) return;
    setRegenerating(true);
    setError(null);
    try {
      const result = await window.electronAPI.regenStructCensus(full);
      if (result.exitCode !== 0) {
        setError(`struct_census.py exited with code ${result.exitCode}`);
      }
      await load();
    } catch (e) {
      setError(e.message);
    } finally {
      setRegenerating(false);
    }
  };

  const modules = useMemo(
    () => [...new Set(records.map(r => r.module))].sort(), [records]);
  const kinds = useMemo(
    () => [...new Set(records.map(r => r.kind))].sort(), [records]);

  // per-module rollup drives the summary strip; doubles as a filter control
  const moduleSummary = useMemo(() => {
    const acc = new Map();
    for (const r of records) {
      const e = acc.get(r.module) || { module: r.module, count: 0, bytes: 0 };
      e.count++;
      e.bytes += r.size || 0;
      acc.set(r.module, e);
    }
    return [...acc.values()].sort((a, b) => b.count - a.count);
  }, [records]);

  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    const rows = records.filter(r =>
      (!moduleFilter || r.module === moduleFilter) &&
      (!kindFilter || r.kind === kindFilter) &&
      (!minSize || (r.size || 0) >= minSize) &&
      (!q || r.name.toLowerCase().includes(q) || r.file.toLowerCase().includes(q))
    );
    rows.sort((a, b) => {
      const x = a[sortCol];
      const y = b[sortCol];
      if (NUMERIC.has(sortCol)) return ((x ?? -1) - (y ?? -1)) * sortDir;
      return String(x).localeCompare(String(y)) * sortDir;
    });
    return rows;
  }, [records, query, moduleFilter, kindFilter, minSize, sortCol, sortDir]);

  const visibleBytes = useMemo(
    () => visible.reduce((s, r) => s + (r.size || 0), 0), [visible]);

  function toggleSort(col) {
    if (col === sortCol) setSortDir(d => -d);
    else { setSortCol(col); setSortDir(NUMERIC.has(col) ? -1 : 1); }
  }

  const regenButton = (
    <button
      className="btn btn-primary"
      onClick={() => handleRegen(false)}
      disabled={regenerating}
      title="Re-run utils/struct_census.py — only re-parses translation units whose dependencies changed"
    >
      {regenerating ? 'Regenerating…' : '⟳ Regen'}
    </button>
  );

  if (loading && !records.length && !missing) {
    return <div className="loading">Loading struct census…</div>;
  }

  if (missing) {
    return (
      <div className="empty-state">
        <p>No struct census report yet.</p>
        <p className="note">vibe/meta/ds/struct_census.csv does not exist.</p>
        <div style={{ marginTop: 12 }}>{regenButton}</div>
        {regenerating && <p className="note">First run parses every translation
          unit and takes about a minute — watch the terminal below.</p>}
        {error && <div className="error-message">{error}</div>}
      </div>
    );
  }

  return (
    <div className="struct-census">
      <div className="census-header">
        <div className="census-title">
          <h3>Struct Census</h3>
          <span className="census-meta">
            {records.length.toLocaleString()} records across {modules.length} modules
            {generated && ` · generated ${new Date(generated).toLocaleString()}`}
          </span>
        </div>
        {regenButton}
      </div>

      <div className="census-modules">
        {moduleSummary.map(m => (
          <button
            key={m.module}
            className={`census-chip ${moduleFilter === m.module ? 'active' : ''}`}
            onClick={() => setModuleFilter(moduleFilter === m.module ? '' : m.module)}
            title={`${m.bytes.toLocaleString()} bytes of record payload`}
          >
            <span className="chip-name">{m.module}</span>
            <span className="chip-count">{m.count}</span>
          </button>
        ))}
      </div>

      <div className="census-filters">
        <input
          type="text"
          className="census-search"
          placeholder="Filter name or file…"
          value={query}
          onChange={e => setQuery(e.target.value)}
        />
        <select value={moduleFilter} onChange={e => setModuleFilter(e.target.value)}>
          <option value="">all modules</option>
          {modules.map(m => <option key={m} value={m}>{m}</option>)}
        </select>
        <select value={kindFilter} onChange={e => setKindFilter(e.target.value)}>
          <option value="">all kinds</option>
          {kinds.map(k => <option key={k} value={k}>{k}</option>)}
        </select>
        <select value={minSize} onChange={e => setMinSize(Number(e.target.value))}>
          <option value={0}>any size</option>
          <option value={64}>≥ 64 B</option>
          <option value={256}>≥ 256 B</option>
          <option value={1024}>≥ 1 KB</option>
          <option value={65536}>≥ 64 KB</option>
        </select>
        <span className="census-count">
          {visible.length.toLocaleString()} of {records.length.toLocaleString()} ·{' '}
          {visibleBytes.toLocaleString()} B
        </span>
      </div>

      {error && <div className="error-message">{error}</div>}

      <div className="census-table-wrap">
        <table className="census-table">
          <thead>
            <tr>
              {columns.map(c => (
                <th
                  key={c}
                  onClick={() => toggleSort(c)}
                  className={`${NUMERIC.has(c) ? 'num' : ''} ${sortCol === c ? (sortDir > 0 ? 'asc' : 'desc') : ''}`}
                >
                  {c}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {visible.map((r, i) => (
              <tr key={`${r.file}:${r.line}:${r.name}:${i}`}>
                {columns.map(c => (
                  <td key={c} className={NUMERIC.has(c) ? 'num' : c === 'name' ? 'name' : c === 'file' ? 'loc' : ''}>
                    {r[c] === null ? '' : r[c]}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

export default StructCensusPanel;
