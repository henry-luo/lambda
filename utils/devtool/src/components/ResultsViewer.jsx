import React from 'react';

function ResultsViewer({ results }) {
  if (!results) return null;

  return (
    <div className="results-viewer">
      <div className="results-header">
        <h3>Test Results</h3>
      </div>

      <div className="results-content">
        <div className="result-section">
          <h4>Exit Code</h4>
          <div className={`exit-code ${results.exitCode === 0 ? 'success' : 'error'}`}>
            {results.exitCode}
          </div>
        </div>

        {results.lambdaOutput && (
          <div className="result-section">
            <h4>Lambda Output</h4>
            <div className="json-viewer">
              <pre>{JSON.stringify(results.lambdaOutput, null, 2)}</pre>
            </div>
          </div>
        )}

        {results.stdout && (
          <div className="result-section">
            <h4>Standard Output</h4>
            <pre className="output-text">{results.stdout}</pre>
          </div>
        )}

        {results.stderr && (
          <div className="result-section">
            <h4>Standard Error</h4>
            <pre className="output-text error">{results.stderr}</pre>
          </div>
        )}
      </div>
    </div>
  );
}

export default ResultsViewer;
