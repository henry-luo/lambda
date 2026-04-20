import React, { useState, useEffect } from 'react';

function LogViewer() {
  const [logContent, setLogContent] = useState('');
  const [filter, setFilter] = useState('');
  const [autoRefresh, setAutoRefresh] = useState(false);

  useEffect(() => {
    loadLog();
  }, []);

  useEffect(() => {
    if (!autoRefresh) return;

    const interval = setInterval(() => {
      loadLog();
    }, 1000);

    return () => clearInterval(interval);
  }, [autoRefresh]);

  async function loadLog() {
    try {
      const content = await window.electronAPI.readLogFile();
      setLogContent(content);
    } catch (error) {
      console.error('Failed to load log:', error);
      setLogContent('Error loading log file');
    }
  }

  function getFilteredLines() {
    if (!logContent) return [];

    const lines = logContent.split('\n');
    if (!filter.trim()) return lines;

    const filterLower = filter.toLowerCase();
    return lines.filter(line => line.toLowerCase().includes(filterLower));
  }

  const filteredLines = getFilteredLines();

  return (
    <div className="log-viewer">
      <div className="log-header">
        <input
          type="text"
          className="log-filter-input"
          placeholder="Filter log lines..."
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
        />
        <div className="log-controls">
          <label className="auto-refresh-label">
            <input
              type="checkbox"
              checked={autoRefresh}
              onChange={(e) => setAutoRefresh(e.target.checked)}
            />
            Auto-refresh
          </label>
          <button className="btn btn-small" onClick={loadLog}>
            Refresh
          </button>
          <button className="btn btn-small" onClick={() => setLogContent('')}>
            Clear
          </button>
        </div>
      </div>
      <div className="log-content">
        <pre className="log-text">
          {filteredLines.length > 0 ? filteredLines.join('\n') : 'No log entries'}
        </pre>
      </div>
      <div className="log-footer">
        {filter && (
          <span className="log-stats">
            Showing {filteredLines.length} of {logContent.split('\n').length} lines
          </span>
        )}
      </div>
    </div>
  );
}

export default LogViewer;
