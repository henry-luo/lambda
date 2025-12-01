import React, { useState, useEffect, useImperativeHandle, forwardRef } from 'react';

const HtmlSourceViewer = forwardRef(({ testPath }, ref) => {
  const [htmlContent, setHtmlContent] = useState('');
  const [error, setError] = useState(null);

  useEffect(() => {
    console.log('HtmlSourceViewer testPath changed:', testPath);
    if (testPath) {
      loadHtmlSource();
    } else {
      setHtmlContent('');
      setError(null);
    }
  }, [testPath]);

  async function loadHtmlSource() {
    if (!testPath) return;
    try {
      setError(null);
      console.log('Loading HTML source for:', testPath);
      const content = await window.electronAPI.readHtmlSource(testPath);
      if (!content) {
        setError(`File not found or empty: ${testPath}`);
        setHtmlContent('');
      } else {
        setHtmlContent(content);
      }
    } catch (err) {
      console.error('Failed to load HTML source:', err);
      setError(`Error loading: ${err.message || testPath}`);
      setHtmlContent('');
    }
  }

  // Expose refresh method via ref
  useImperativeHandle(ref, () => ({
    refresh: loadHtmlSource
  }));

  return (
    <div className="html-source-viewer">
      <div className="html-source-header">
        <span className="html-source-title">
          {testPath ? `HTML Source: ${testPath.split('/').pop()}` : 'HTML Source'}
        </span>
        <div className="html-source-controls">
          <button className="btn btn-small" onClick={loadHtmlSource} disabled={!testPath}>
            Refresh
          </button>
          {htmlContent && (
            <button
              className="btn btn-small"
              onClick={() => navigator.clipboard.writeText(htmlContent)}
            >
              Copy
            </button>
          )}
        </div>
      </div>
      <div className="html-source-content">
        {error ? (
          <div className="error-message">{error}</div>
        ) : htmlContent ? (
          <pre className="html-source-text">{htmlContent}</pre>
        ) : (
          <div className="empty-state">
            <p>Select a test to view HTML source</p>
          </div>
        )}
      </div>
    </div>
  );
});

HtmlSourceViewer.displayName = 'HtmlSourceViewer';

export default HtmlSourceViewer;
