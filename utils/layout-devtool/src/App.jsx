import React, { useState, useRef, useEffect } from 'react';
import TestTree from './components/TestTree';
import ComparisonPanel from './components/ComparisonPanel';
import BottomPanel from './components/BottomPanel';

function App() {
  const [selectedTest, setSelectedTest] = useState(null);
  const [testResults, setTestResults] = useState(null);
  const [isRunning, setIsRunning] = useState(false);
  const [leftPanelWidth, setLeftPanelWidth] = useState(300);
  const [terminalHeight, setTerminalHeight] = useState(250);
  const [lambdaRenderPath, setLambdaRenderPath] = useState(null);
  const [lambdaPixelRatio, setLambdaPixelRatio] = useState(1);
  const [recentTests, setRecentTests] = useState([]);
  const [showRecentTests, setShowRecentTests] = useState(false);
  const [viewportPreset, setViewportPreset] = useState('desktop'); // desktop, tablet, mobile, custom
  const terminalRef = useRef(null);
  const comparisonPanelRef = useRef(null);
  const isDraggingLeftRef = useRef(false);
  const isDraggingBottomRef = useRef(false);

  // Viewport presets
  const viewportPresets = {
    desktop: { width: 1200, height: 800, label: 'Desktop (1200×800)' },
    tablet: { width: 768, height: 1024, label: 'Tablet (768×1024)' },
    mobile: { width: 375, height: 667, label: 'Mobile (375×667)' },
    custom: { width: 1200, height: 800, label: 'Custom' }
  };

  // Load recent tests on mount
  useEffect(() => {
    window.electronAPI.getRecentTests().then(tests => {
      setRecentTests(tests || []);
    });
  }, []);

  // Keyboard shortcuts
  useEffect(() => {
    const handleKeyDown = (e) => {
      // Cmd+R or Ctrl+R: Run selected test
      if ((e.metaKey || e.ctrlKey) && e.key === 'r') {
        e.preventDefault();
        if (selectedTest && !isRunning) {
          handleRunTest();
        }
      }
      // Cmd+L or Ctrl+L: Focus log panel
      if ((e.metaKey || e.ctrlKey) && e.key === 'l') {
        e.preventDefault();
        terminalRef.current?.focus();
      }
      // Cmd+K or Ctrl+K: Clear terminal
      if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
        e.preventDefault();
        terminalRef.current?.clear();
      }
      // Cmd+Shift+R or Ctrl+Shift+R: Show recent tests
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key === 'R') {
        e.preventDefault();
        setShowRecentTests(!showRecentTests);
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [selectedTest, isRunning, showRecentTests]);

  const handleTestSelect = (test) => {
    setSelectedTest(test);
    setTestResults(null);
    setLambdaRenderPath(null);
    
    // Add to recent tests
    window.electronAPI.addRecentTest(test).then(updated => {
      setRecentTests(updated || []);
    });
  };

  const handleRunTest = async () => {
    if (!selectedTest || isRunning) return;

    console.log('Running test:', selectedTest);
    setIsRunning(true);
    setLambdaRenderPath(null);
    terminalRef.current?.clear();
    terminalRef.current?.writeln(`Running test: ${selectedTest.category}/${selectedTest.testFile}`);
    terminalRef.current?.writeln('');

    try {
      const testPath = `test/layout/data/${selectedTest.category}/${selectedTest.testFile}`;
      console.log('Test path:', testPath);

      // Get viewport dimensions from preset
      const preset = viewportPresets[viewportPreset];
      let viewportWidth = preset.width;
      let viewportHeight = preset.height;

      // Override with browser panel dimensions if available
      if (comparisonPanelRef.current?.getBrowserDimensions) {
        const dims = comparisonPanelRef.current.getBrowserDimensions();
        viewportWidth = dims.width;
      }

      // Measure actual page content height via main process
      terminalRef.current?.writeln('Measuring page content height...');
      try {
        viewportHeight = await window.electronAPI.measurePageHeight(testPath, viewportWidth);
        console.log('Measured page height:', viewportHeight);
      } catch (measureError) {
        console.error('Failed to measure page height:', measureError);
      }
      terminalRef.current?.writeln(`Viewport: ${viewportWidth}×${viewportHeight} (${preset.label})`);

      // Render the Lambda view with browser panel dimensions
      terminalRef.current?.writeln('Rendering Lambda view...');
      try {
        const renderResult = await window.electronAPI.renderLambdaView(testPath, viewportWidth, viewportHeight);
        console.log('Render result:', renderResult);
        setLambdaRenderPath(renderResult.path);
        setLambdaPixelRatio(renderResult.pixelRatio || 1);
        terminalRef.current?.writeln(`\x1b[32m✓ Render completed (pixel ratio: ${renderResult.pixelRatio})\x1b[0m`);
      } catch (renderError) {
        console.error('Render error:', renderError);
        terminalRef.current?.writeln(`\x1b[31m✗ Render failed: ${renderError.message}\x1b[0m`);
      }

      terminalRef.current?.writeln('');
      terminalRef.current?.writeln('Running layout comparison...');

      const result = await window.electronAPI.runTest(`./test/layout/data/${selectedTest.category}/${selectedTest.testFile}`);
      console.log('Test result:', result);

      terminalRef.current?.writeln('');
      if (result.exitCode === 0) {
        terminalRef.current?.writeln('\x1b[32mTest completed successfully\x1b[0m');
      } else {
        terminalRef.current?.writeln(`\x1b[31mTest failed with exit code: ${result.exitCode}\x1b[0m`);
      }

      setTestResults(result);

      // Refresh the View Tree panel and HTML Tree panel
      terminalRef.current?.refreshViewTree();
      terminalRef.current?.refreshHtmlTree();
    } catch (error) {
      console.error('Test execution error:', error);
      terminalRef.current?.writeln(`\x1b[31mError: ${error.message}\x1b[0m`);
    } finally {
      setIsRunning(false);
    }
  };

  // Handle left panel resize
  const handleLeftMouseDown = (e) => {
    isDraggingLeftRef.current = true;
    document.body.classList.add('resizing');
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
    e.preventDefault();
  };

  const handleLeftMouseMove = (e) => {
    if (!isDraggingLeftRef.current) return;
    const newWidth = Math.max(200, Math.min(600, e.clientX));
    setLeftPanelWidth(newWidth);
  };

  const handleLeftMouseUp = () => {
    if (isDraggingLeftRef.current) {
      isDraggingLeftRef.current = false;
      document.body.classList.remove('resizing');
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
    }
  };

  // Handle terminal resize
  const handleBottomMouseDown = (e) => {
    isDraggingBottomRef.current = true;
    document.body.classList.add('resizing');
    document.body.style.cursor = 'row-resize';
    document.body.style.userSelect = 'none';
    e.preventDefault();
  };

  const handleBottomMouseMove = (e) => {
    if (!isDraggingBottomRef.current) return;
    const newHeight = Math.max(150, Math.min(600, window.innerHeight - e.clientY));
    setTerminalHeight(newHeight);
  };

  const handleBottomMouseUp = () => {
    if (isDraggingBottomRef.current) {
      isDraggingBottomRef.current = false;
      document.body.classList.remove('resizing');
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
    }
  };

  useEffect(() => {
    document.addEventListener('mousemove', handleLeftMouseMove);
    document.addEventListener('mouseup', handleLeftMouseUp);
    document.addEventListener('mousemove', handleBottomMouseMove);
    document.addEventListener('mouseup', handleBottomMouseUp);
    return () => {
      document.removeEventListener('mousemove', handleLeftMouseMove);
      document.removeEventListener('mouseup', handleLeftMouseUp);
      document.removeEventListener('mousemove', handleBottomMouseMove);
      document.removeEventListener('mouseup', handleBottomMouseUp);
    };
  }, []);

  return (
    <div className="app">
      <div className="menu-bar">
        <div className="menu-items">
          <span className="menu-item">File</span>
          <span 
            className="menu-item" 
            onClick={() => setShowRecentTests(!showRecentTests)}
            style={{ cursor: 'pointer' }}
          >
            Recent {showRecentTests && '▼'}
          </span>
          <span className="menu-item">View</span>
          <span className="menu-item">Help</span>
        </div>
        <div className="toolbar">
          <div style={{ display: 'flex', gap: '8px', alignItems: 'center', marginRight: '12px' }}>
            <label style={{ fontSize: '13px', color: '#ccc' }}>Viewport:</label>
            <select 
              value={viewportPreset} 
              onChange={(e) => setViewportPreset(e.target.value)}
              style={{ 
                padding: '4px 8px', 
                borderRadius: '4px', 
                border: '1px solid #555',
                background: '#2d2d2d',
                color: '#ccc',
                fontSize: '13px'
              }}
            >
              <option value="desktop">{viewportPresets.desktop.label}</option>
              <option value="tablet">{viewportPresets.tablet.label}</option>
              <option value="mobile">{viewportPresets.mobile.label}</option>
            </select>
          </div>
          <button
            className="btn btn-primary"
            onClick={handleRunTest}
            disabled={!selectedTest || isRunning}
            title="Run test (Cmd+R)"
          >
            {isRunning ? 'Running...' : '▶ Run Test'}
          </button>
        </div>
      </div>

      {showRecentTests && recentTests.length > 0 && (
        <div style={{
          position: 'absolute',
          top: '40px',
          left: '60px',
          background: '#2d2d2d',
          border: '1px solid #555',
          borderRadius: '4px',
          padding: '8px',
          zIndex: 1000,
          minWidth: '250px',
          boxShadow: '0 4px 12px rgba(0,0,0,0.5)'
        }}>
          <div style={{ fontSize: '12px', color: '#888', marginBottom: '8px', padding: '4px 8px' }}>
            Recent Tests (Cmd+Shift+R)
          </div>
          {recentTests.map((test, idx) => (
            <div
              key={idx}
              onClick={() => {
                handleTestSelect(test);
                setShowRecentTests(false);
              }}
              style={{
                padding: '6px 8px',
                cursor: 'pointer',
                fontSize: '13px',
                color: '#ccc',
                borderRadius: '3px',
                transition: 'background 0.2s'
              }}
              onMouseEnter={(e) => e.target.style.background = '#3d3d3d'}
              onMouseLeave={(e) => e.target.style.background = 'transparent'}
            >
              <div style={{ fontWeight: 500 }}>{test.testFile}</div>
              <div style={{ fontSize: '11px', color: '#888' }}>{test.category}</div>
            </div>
          ))}
        </div>
      )}

      <div className="main-content">
        <div className="left-sidebar" style={{ width: `${leftPanelWidth}px` }}>
          <TestTree
            onTestSelect={handleTestSelect}
            selectedTest={selectedTest}
          />
        </div>
        <div className="resize-handle-vertical" onMouseDown={handleLeftMouseDown} />

        <div className="right-content">
          <div className="top-panel">
            <ComparisonPanel ref={comparisonPanelRef} test={selectedTest} lambdaRenderPath={lambdaRenderPath} lambdaPixelRatio={lambdaPixelRatio} />
          </div>
          <div className="resize-handle-horizontal" onMouseDown={handleBottomMouseDown} />
          <div className="bottom-panel" style={{ height: `${terminalHeight}px` }}>
            <BottomPanel
              ref={terminalRef}
              testPath={selectedTest ? `test/layout/data/${selectedTest.category}/${selectedTest.testFile}` : null}
            />
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;
