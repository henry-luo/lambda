import React, { useState, useEffect } from 'react';

function ViewTreeViewer() {
  const [viewTreeContent, setViewTreeContent] = useState('');
  const [isJson, setIsJson] = useState(true);

  useEffect(() => {
    loadViewTree();
  }, []);

  async function loadViewTree() {
    try {
      const content = await window.electronAPI.readViewTreeFile();
      setViewTreeContent(content);

      // Try to parse as JSON to determine if it's valid JSON
      try {
        JSON.parse(content);
        setIsJson(true);
      } catch {
        setIsJson(false);
      }
    } catch (error) {
      console.error('Failed to load view tree:', error);
      setViewTreeContent('Error loading view_tree.txt file');
      setIsJson(false);
    }
  }

  function formatJson(jsonStr) {
    try {
      const obj = JSON.parse(jsonStr);
      return JSON.stringify(obj, null, 2);
    } catch {
      return jsonStr;
    }
  }

  return (
    <div className="view-tree-viewer">
      <div className="view-tree-header">
        <span className="view-tree-title">View Tree Output</span>
        <div className="view-tree-controls">
          <button className="btn btn-small" onClick={loadViewTree}>
            Refresh
          </button>
          {isJson && (
            <button
              className="btn btn-small"
              onClick={() => {
                const formatted = formatJson(viewTreeContent);
                navigator.clipboard.writeText(formatted);
              }}
            >
              Copy JSON
            </button>
          )}
        </div>
      </div>
      <div className="view-tree-content">
        <pre className="view-tree-text">
          {viewTreeContent ? (isJson ? formatJson(viewTreeContent) : viewTreeContent) : 'No view tree data'}
        </pre>
      </div>
    </div>
  );
}

export default ViewTreeViewer;
