import React, { useState, useEffect, useRef } from 'react';

function ComparisonPanel({ test, lambdaRenderPath }) {
  const [browserView, setBrowserView] = useState(null);
  const [splitPos, setSplitPos] = useState(50);
  const isDragging = useRef(false);
  const containerRef = useRef(null);

  useEffect(() => {
    if (test) {
      loadViews(test);
    } else {
      setBrowserView(null);
    }
  }, [test]);

  async function loadViews(test) {
    try {
      // Get absolute path for the test file
      // The path is relative to the project root where main.js resolves it
      const testPath = `test/layout/data/${test.category}/${test.testFile}`;

      // For browser view, we need to construct a file:// URL
      // Since we're in the renderer, we can use the file protocol
      const absolutePath = await getAbsolutePath(testPath);
      setBrowserView(`file://${absolutePath}`);
    } catch (error) {
      console.error('Failed to load views:', error);
    }
  }

  async function getAbsolutePath(relativePath) {
    // In a real implementation, this would come from the main process
    // For now, construct it based on the known project structure
    return `/Users/henryluo/Projects/Jubily/${relativePath}`;
  }

  function handleMouseDown(e) {
    e.preventDefault();
    isDragging.current = true;
    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
  }

  function handleMouseMove(e) {
    if (!isDragging.current || !containerRef.current) return;

    const rect = containerRef.current.getBoundingClientRect();
    const pos = ((e.clientX - rect.left) / rect.width) * 100;
    setSplitPos(Math.max(20, Math.min(80, pos)));
  }

  function handleMouseUp() {
    isDragging.current = false;
    document.removeEventListener('mousemove', handleMouseMove);
    document.removeEventListener('mouseup', handleMouseUp);
  }

  if (!test) {
    return (
      <div className="comparison-panel empty">
        <div className="empty-state">
          <p>Select a test from the tree to view comparison</p>
        </div>
      </div>
    );
  }

  return (
    <div className="comparison-panel" ref={containerRef}>
      <div className="panel left-panel" style={{ width: `${splitPos}%` }}>
        <div className="panel-header">
          <span>Browser View</span>
          <span className="panel-info">{test.testFile}</span>
        </div>
        <div className="panel-content">
          {browserView ? (
            <iframe
              src={browserView}
              title="Browser View"
              className="browser-iframe"
              sandbox="allow-same-origin allow-scripts"
            />
          ) : (
            <div className="loading">Loading...</div>
          )}
        </div>
      </div>

      <div
        className="gutter"
        onMouseDown={handleMouseDown}
      >
        <div className="gutter-handle" />
      </div>

      <div className="panel right-panel" style={{ width: `${100 - splitPos}%` }}>
        <div className="panel-header">
          <span>Lambda View</span>
          <span className="panel-info">Rendered Output</span>
        </div>
        <div className="panel-content">
          {lambdaRenderPath ? (
            <img
              src={lambdaRenderPath}
              alt="Lambda Render"
              className="lambda-render"
            />
          ) : (
            <div className="empty-state">
              <p>Run test to see Lambda output</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

export default ComparisonPanel;
