import React, { useState, useEffect, useImperativeHandle, forwardRef } from 'react';

const HtmlTreeViewer = forwardRef((props, ref) => {
  const [htmlTreeContent, setHtmlTreeContent] = useState('');
  const [filteredContent, setFilteredContent] = useState('');
  const [filterText, setFilterText] = useState('');
  const [error, setError] = useState(null);
  const [lineCount, setLineCount] = useState(0);
  const [filteredLineCount, setFilteredLineCount] = useState(0);

  useEffect(() => {
    applyFilter();
  }, [htmlTreeContent, filterText]);

  function applyFilter() {
    if (!htmlTreeContent) {
      setFilteredContent('');
      setLineCount(0);
      setFilteredLineCount(0);
      return;
    }

    const lines = htmlTreeContent.split('\n');
    setLineCount(lines.length);

    if (!filterText.trim()) {
      setFilteredContent(htmlTreeContent);
      setFilteredLineCount(lines.length);
      return;
    }

    const filterLower = filterText.toLowerCase();
    const filtered = lines.filter(line => 
      line.toLowerCase().includes(filterLower)
    );

    setFilteredContent(filtered.join('\n'));
    setFilteredLineCount(filtered.length);
  }

  async function loadHtmlTree() {
    try {
      setError(null);
      console.log('Loading html_tree.txt...');
      const content = await window.electronAPI.readHtmlTreeFile();
      if (!content) {
        setError('File not found or empty: html_tree.txt');
        setHtmlTreeContent('');
      } else {
        setHtmlTreeContent(content);
      }
    } catch (err) {
      console.error('Failed to load html_tree.txt:', err);
      setError(`Error loading: ${err.message || 'html_tree.txt'}`);
      setHtmlTreeContent('');
    }
  }

  // Expose refresh method via ref
  useImperativeHandle(ref, () => ({
    refresh: loadHtmlTree
  }));

  function handleFilterChange(e) {
    setFilterText(e.target.value);
  }

  function clearFilter() {
    setFilterText('');
  }

  return (
    <div className="html-tree-viewer">
      <div className="html-tree-header">
        <span className="html-tree-title">HTML Tree</span>
        <div className="html-tree-controls">
          <input
            type="text"
            className="filter-input"
            placeholder="Filter lines..."
            value={filterText}
            onChange={handleFilterChange}
          />
          {filterText && (
            <button className="btn btn-small" onClick={clearFilter}>
              Clear
            </button>
          )}
          <button className="btn btn-small" onClick={loadHtmlTree}>
            Refresh
          </button>
          {htmlTreeContent && (
            <button
              className="btn btn-small"
              onClick={() => navigator.clipboard.writeText(filteredContent || htmlTreeContent)}
            >
              Copy
            </button>
          )}
          {filterText && (
            <span className="line-count">
              {filteredLineCount} / {lineCount} lines
            </span>
          )}
        </div>
      </div>
      <div className="html-tree-content">
        {error ? (
          <div className="error-message">{error}</div>
        ) : filteredContent || htmlTreeContent ? (
          <pre className="html-tree-text">{filteredContent || htmlTreeContent}</pre>
        ) : (
          <div className="empty-state">
            <p>Run a test to generate HTML tree</p>
            <p className="hint">The html_tree.txt file will be loaded automatically after test execution</p>
          </div>
        )}
      </div>
    </div>
  );
});

HtmlTreeViewer.displayName = 'HtmlTreeViewer';

export default HtmlTreeViewer;
